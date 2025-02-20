// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_H_
#define V8_ZONE_ZONE_H_

#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/type-stats.h"
#include "src/zone/zone-segment.h"
#include "src/zone/zone-type-traits.h"

#ifndef ZONE_NAME
#define ZONE_NAME __func__
#endif

namespace v8 {
namespace internal {

// The Zone supports very fast allocation of small chunks of
// memory. The chunks cannot be deallocated individually, but instead
// the Zone supports deallocating all chunks in one fast
// operation. The Zone is used to hold temporary data structures like
// the abstract syntax tree, which is deallocated after compilation.
//
// Note: There is no need to initialize the Zone; the first time an
// allocation is attempted, a segment of memory will be requested
// through the allocator.
//
// Note: The implementation is inherently not thread safe. Do not use
// from multi-threaded code.

class ZoneSnapshot;

class V8_EXPORT_PRIVATE Zone final {
 public:
  Zone(AccountingAllocator* allocator, const char* name,
       bool support_compression = false);
  ~Zone();

  // Returns true if the zone supports zone pointer compression.
  bool supports_compression() const {
    return COMPRESS_ZONES_BOOL && supports_compression_;
  }

  // Allocate 'size' bytes of uninitialized memory in the Zone; expands the Zone
  // by allocating new segments of memory on demand using AccountingAllocator
  // (see AccountingAllocator::AllocateSegment()).
  //
  // When V8_ENABLE_PRECISE_ZONE_STATS is defined, the allocated bytes are
  // associated with the provided TypeTag type.
  template <typename TypeTag>
  void* Allocate(size_t size) {
#ifdef V8_USE_ADDRESS_SANITIZER
    return AsanNew(size);
#else
    size = RoundUp(size, kAlignmentInBytes);
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    if (V8_UNLIKELY(TracingFlags::is_zone_stats_enabled())) {
      type_stats_.AddAllocated<TypeTag>(size);
    }
    allocation_size_for_tracing_ += size;
#endif
    if (V8_UNLIKELY(size > limit_ - position_)) {
      Expand(size);
    }

    DCHECK_LE(position_, limit_);
    DCHECK_LE(size, limit_ - position_);
    DCHECK_EQ(0, position_ % kAlignmentInBytes);
    void* result = reinterpret_cast<void*>(position_);
    position_ += size;
    return result;
#endif  // V8_USE_ADDRESS_SANITIZER
  }

  // Return 'size' bytes of memory back to Zone. These bytes can be reused
  // for following allocations.
  //
  // When V8_ENABLE_PRECISE_ZONE_STATS is defined, the deallocated bytes are
  // associated with the provided TypeTag type.
  template <typename TypeTag = void>
  void Delete(void* pointer, size_t size) {
    DCHECK_NOT_NULL(pointer);
    DCHECK_NE(size, 0);
    size = RoundUp(size, kAlignmentInBytes);
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    if (V8_UNLIKELY(TracingFlags::is_zone_stats_enabled())) {
      type_stats_.AddDeallocated<TypeTag>(size);
    }
    freed_size_for_tracing_ += size;
#endif

#ifdef DEBUG
    static const unsigned char kZapDeadByte = 0xcd;
    memset(pointer, kZapDeadByte, size);
#endif
  }

  // Allocates memory for T instance and constructs object by calling respective
  // Args... constructor.
  //
  // When V8_ENABLE_PRECISE_ZONE_STATS is defined, the allocated bytes are
  // associated with the T type.
  template <typename T, typename... Args>
  T* New(Args&&... args) {
    static_assert(alignof(T) <= kAlignmentInBytes);
    void* memory = Allocate<T>(sizeof(T));
    return new (memory) T(std::forward<Args>(args)...);
  }

  // Allocates uninitialized memory for 'length' number of T instances.
  //
  // When V8_ENABLE_PRECISE_ZONE_STATS is defined, the allocated bytes are
  // associated with the provided TypeTag type. It might be useful to tag
  // buffer allocations with meaningful names to make buffer allocation sites
  // distinguishable between each other.
  template <typename T, typename TypeTag = T[]>
  T* AllocateArray(size_t length) {
    static_assert(alignof(T) <= kAlignmentInBytes);
    DCHECK_IMPLIES(is_compressed_pointer<T>::value, supports_compression());
    DCHECK_LT(length, std::numeric_limits<size_t>::max() / sizeof(T));
    return static_cast<T*>(Allocate<TypeTag>(length * sizeof(T)));
  }

  // Allocates a Vector with 'length' uninitialized entries.
  template <typename T, typename TypeTag = T[]>
  base::Vector<T> AllocateVector(size_t length) {
    T* new_array = AllocateArray<T, TypeTag>(length);
    return {new_array, length};
  }

  // Allocates a Vector with 'length' elements and value-constructs them.
  template <typename T, typename TypeTag = T[]>
  base::Vector<T> NewVector(size_t length) {
    T* new_array = AllocateArray<T, TypeTag>(length);
    std::uninitialized_value_construct_n(new_array, length);
    return {new_array, length};
  }

  // Allocates a Vector with 'length' elements and initializes them with
  // 'value'.
  template <typename T, typename TypeTag = T[]>
  base::Vector<T> NewVector(size_t length, T value) {
    T* new_array = AllocateArray<T, TypeTag>(length);
    std::uninitialized_fill_n(new_array, length, value);
    return {new_array, length};
  }

  template <typename T, typename TypeTag = std::remove_const_t<T>[]>
  base::Vector<std::remove_const_t<T>> CloneVector(base::Vector<T> v) {
    auto* new_array = AllocateArray<std::remove_const_t<T>, TypeTag>(v.size());
    std::uninitialized_copy(v.begin(), v.end(), new_array);
    return {new_array, v.size()};
  }

  // Return array of 'length' elements back to Zone. These bytes can be reused
  // for following allocations.
  //
  // When V8_ENABLE_PRECISE_ZONE_STATS is defined, the deallocated bytes are
  // associated with the provided TypeTag type.
  template <typename T, typename TypeTag = T[]>
  void DeleteArray(T* pointer, size_t length) {
    Delete<TypeTag>(pointer, length * sizeof(T));
  }

  // Seals the zone to prevent any further allocation.
  void Seal() { sealed_ = true; }

  // Allows the zone to be safely reused. Releases the memory except for the
  // last page, and fires zone destruction and creation events for the
  // accounting allocator.
  void Reset();

  size_t segment_bytes_allocated() const { return segment_bytes_allocated_; }

  const char* name() const { return name_; }

  // Returns precise value of used zone memory, allowed to be called only
  // from thread owning the zone.
  size_t allocation_size() const {
    size_t extra = segment_head_ ? position_ - segment_head_->start() : 0;
    return allocation_size_ + extra;
  }

  // When V8_ENABLE_PRECISE_ZONE_STATS is not defined, returns used zone memory
  // not including the head segment.
  // Can be called from threads not owning the zone.
  size_t allocation_size_for_tracing() const {
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    return allocation_size_for_tracing_;
#else
    return allocation_size_;
#endif
  }

  // Returns number of bytes freed in this zone via Delete<T>()/DeleteArray<T>()
  // calls. Returns non-zero values only when V8_ENABLE_PRECISE_ZONE_STATS is
  // defined.
  size_t freed_size_for_tracing() const {
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    return freed_size_for_tracing_;
#else
    return 0;
#endif
  }

  AccountingAllocator* allocator() const { return allocator_; }

#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  const TypeStats& type_stats() const { return type_stats_; }
#endif

#ifdef DEBUG
  bool Contains(const void* ptr) const;
#endif

  V8_WARN_UNUSED_RESULT ZoneSnapshot Snapshot() const;

 private:
  void* AsanNew(size_t size);

  // Deletes all objects and free all memory allocated in the Zone.
  void DeleteAll();

  // Releases the current segment without performing any local bookkeeping
  // (e.g. tracking allocated bytes, maintaining linked lists, etc).
  void ReleaseSegment(Segment* segment);

  // All pointers returned from New() are 8-byte aligned.
  // ASan requires 8-byte alignment. MIPS also requires 8-byte alignment.
  static const size_t kAlignmentInBytes = 8;

  // Never allocate segments smaller than this size in bytes.
  static const size_t kMinimumSegmentSize = 8 * KB;

  // Never allocate segments larger than this size in bytes.
  static const size_t kMaximumSegmentSize = 32 * KB;

  // The number of bytes allocated in this zone so far.
  std::atomic<size_t> allocation_size_ = {0};

  // The number of bytes allocated in segments.  Note that this number
  // includes memory allocated from the OS but not yet allocated from
  // the zone.
  std::atomic<size_t> segment_bytes_allocated_ = {0};

  // Expand the Zone to hold at least 'size' more bytes.
  // Should only be called if there is not enough room in the Zone already.
  V8_NOINLINE V8_PRESERVE_MOST void Expand(size_t size);

  // The free region in the current (front) segment is represented as
  // the half-open interval [position, limit). The 'position' variable
  // is guaranteed to be aligned as dictated by kAlignment.
  Address position_ = 0;
  Address limit_ = 0;

  AccountingAllocator* allocator_;

  Segment* segment_head_ = nullptr;
  const char* name_;
  const bool supports_compression_;
  bool sealed_ = false;

#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  TypeStats type_stats_;
  std::atomic<size_t> allocation_size_for_tracing_ = {0};

  // The number of bytes freed in this zone so far.
  std::atomic<size_t> freed_size_for_tracing_ = {0};
#endif

  friend class ZoneSnapshot;
};

// A `ZoneSnapshot` stores the allocation state of a zone. The zone can later be
// reset to that state, effectively deleting all memory which was allocated in
// the zone after taking the snapshot.
// See `ZoneScope` for an example usage of `ZoneSnapshot`.
class ZoneSnapshot final {
 public:
  // Reset the `Zone` from which this snapshot was taken to the state stored in
  // this snapshot.
  void Restore(Zone* zone) const;

 private:
  explicit ZoneSnapshot(const Zone* zone);
  friend class Zone;

#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  const size_t allocation_size_for_tracing_;
  const size_t freed_size_for_tracing_;
#endif
  const size_t allocation_size_;
  const size_t segment_bytes_allocated_;
  const Address position_;
  const Address limit_;
  Segment* const segment_head_;
};

// Similar to the HandleScope, the ZoneScope defines a region of validity for
// zone memory. All memory allocated in the given Zone during the scope's
// lifetime is freed when the scope is destructed, i.e. the Zone is reset to
// the state it was in when the scope was created.
class ZoneScope final {
 public:
  explicit ZoneScope(Zone* zone) : zone_(zone), snapshot_(zone->Snapshot()) {}

  ~ZoneScope() { snapshot_.Restore(zone_); }

 private:
  Zone* const zone_;
  const ZoneSnapshot snapshot_;
};

// ZoneObject is an abstraction that helps define classes of objects
// allocated in the Zone. Use it as a base class; see ast.h.
class ZoneObject {
 public:
  // The accidential old-style pattern
  //    new (zone) SomeObject(...)
  // now produces compilation error. The proper way of allocating objects in
  // Zones looks like this:
  //    zone->New<SomeObject>(...)
  void* operator new(size_t, Zone*) = delete;  // See explanation above.
  // Allow non-allocating placement new.
  void* operator new(size_t size, void* ptr) {  // See explanation above.
    return ptr;
  }

  // Ideally, the delete operator should be private instead of
  // public, but unfortunately the compiler sometimes synthesizes
  // (unused) destructors for classes derived from ZoneObject, which
  // require the operator to be visible. MSVC requires the delete
  // operator to be public.

  // ZoneObjects should never be deleted individually; use
  // Zone::DeleteAll() to delete all zone objects in one go.
  // Note, that descructors will not be called.
  void operator delete(void*, size_t) { UNREACHABLE(); }
  void operator delete(void* pointer, Zone* zone) = delete;
};

// The ZoneAllocationPolicy is used to specialize generic data
// structures to allocate themselves and their elements in the Zone.
class ZoneAllocationPolicy {
 public:
  // Creates unusable allocation policy.
  ZoneAllocationPolicy() : zone_(nullptr) {}
  explicit ZoneAllocationPolicy(Zone* zone) : zone_(zone) {}

  template <typename T, typename TypeTag = T[]>
  V8_INLINE T* AllocateArray(size_t length) {
    return zone()->AllocateArray<T, TypeTag>(length);
  }
  template <typename T, typename TypeTag = T[]>
  V8_INLINE void DeleteArray(T* p, size_t length) {
    zone()->DeleteArray<T, TypeTag>(p, length);
  }

  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
};

}  // namespace internal
}  // namespace v8

// The accidential old-style pattern
//    new (zone) SomeObject(...)
// now produces compilation error. The proper way of allocating objects in
// Zones looks like this:
//    zone->New<SomeObject>(...)
void* operator new(size_t, v8::internal::Zone*) = delete;   // See explanation.
void operator delete(void*, v8::internal::Zone*) = delete;  // See explanation.

#endif  // V8_ZONE_ZONE_H_

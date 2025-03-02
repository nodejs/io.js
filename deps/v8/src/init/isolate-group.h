// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INIT_ISOLATE_GROUP_H_
#define V8_INIT_ISOLATE_GROUP_H_

#include <memory>

#include "include/v8-memory-span.h"
#include "src/base/once.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/external-reference-table.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/memory-chunk-constants.h"
#include "src/sandbox/code-pointer-table.h"
#include "src/utils/allocation.h"

#ifdef V8_ENABLE_LEAPTIERING
#include "src/sandbox/js-dispatch-table.h"
#endif  // V8_ENABLE_LEAPTIERING

namespace v8 {

namespace base {
template <typename T>
class LeakyObject;
}  // namespace base

namespace internal {

#ifdef V8_ENABLE_SANDBOX
class MemoryChunkMetadata;
class Sandbox;
#endif
class CodeRange;
class Isolate;
class ReadOnlyHeap;
class ReadOnlyArtifacts;
class SnapshotData;

// An IsolateGroup allows an API user to control which isolates get allocated
// together in a shared pointer cage.
//
// The standard configuration of V8 is to enable pointer compression and to
// allocate all isolates in a single shared pointer cage
// (V8_COMPRESS_POINTERS_IN_SHARED_CAGE).  This also enables the sandbox
// (V8_ENABLE_SANDBOX), of which there can currently be only one per process, as
// it requires a large part of the virtual address space.
//
// The standard configuration comes with a limitation, in that the total size of
// the compressed pointer cage is limited to 4 GB.  Some API users would like
// pointer compression but also want to avoid the 4 GB limit of the shared
// pointer cage.  Isolate groups allow users to declare which isolates should be
// co-located in a single pointer cage.
//
// Isolate groups are useful only if pointer compression is enabled.  Otherwise,
// the isolate could just allocate pages from the global system allocator;
// there's no need to stay within any particular address range.  If pointer
// compression is disabled, there is just one global isolate group.
//
// Note that JavaScript objects can only be passed between isolates of the same
// group.  Ensuring this invariant is the responsibility of the API user.
class V8_EXPORT_PRIVATE IsolateGroup final {
 public:
  // InitializeOncePerProcess should be called early on to initialize the
  // process-wide group.
  static IsolateGroup* AcquireDefault() { return GetDefault()->Acquire(); }

  // Return true if we can create additional isolate groups: only the case if
  // multiple pointer cages were configured in at build-time.
  static constexpr bool CanCreateNewGroups() {
    return COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL;
  }

  // Create a new isolate group, allocating a fresh pointer cage if pointer
  // compression is enabled.  If new groups cannot be created in this build
  // configuration, abort.
  //
  // The pointer cage for isolates in this group will be released when the
  // group's refcount drops to zero.  The group's initial refcount is 1.
  static IsolateGroup* New();

  static void InitializeOncePerProcess();

  // Obtain a fresh reference on the isolate group.
  IsolateGroup* Acquire() {
    DCHECK_LT(0, reference_count_.load());
    reference_count_++;
    return this;
  }

  // Release a reference on an isolate group, possibly freeing any shared memory
  // resources.
  void Release();

  v8::PageAllocator* page_allocator() const { return page_allocator_; }

#ifdef V8_COMPRESS_POINTERS
  VirtualMemoryCage* GetPtrComprCage() const {
    return pointer_compression_cage_;
  }
  VirtualMemoryCage* GetTrustedPtrComprCage() const {
    return trusted_pointer_compression_cage_;
  }
  Address GetPtrComprCageBase() const { return GetPtrComprCage()->base(); }
  Address GetTrustedPtrComprCageBase() const {
    return GetTrustedPtrComprCage()->base();
  }
#endif  // V8_COMPRESS_POINTERS

  CodeRange* EnsureCodeRange(size_t requested_size);
  CodeRange* GetCodeRange() const { return code_range_.get(); }

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
#ifdef USING_V8_SHARED_PRIVATE
  static IsolateGroup* current() { return current_non_inlined(); }
  static void set_current(IsolateGroup* group) {
    set_current_non_inlined(group);
  }
#else   // !USING_V8_SHARED_PRIVATE
  static IsolateGroup* current() { return current_; }
  static void set_current(IsolateGroup* group) { current_ = group; }
#endif  // USING_V8_SHARED_PRIVATE
#else   // !V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  static IsolateGroup* current() { return GetDefault(); }
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

  MemorySpan<Address> external_ref_table() { return external_ref_table_; }

  bool has_shared_space_isolate() const {
    return shared_space_isolate_ != nullptr;
  }

  Isolate* shared_space_isolate() const {
    return shared_space_isolate_;
  }

  void init_shared_space_isolate(Isolate* isolate) {
    DCHECK(!has_shared_space_isolate());
    shared_space_isolate_ = isolate;
  }

  ReadOnlyHeap* shared_read_only_heap() const { return shared_read_only_heap_; }
  void set_shared_read_only_heap(ReadOnlyHeap* heap) {
    shared_read_only_heap_ = heap;
  }

  base::Mutex* mutex() { return &mutex_; }

  ReadOnlyArtifacts* read_only_artifacts() {
    return read_only_artifacts_.get();
  }

  ReadOnlyArtifacts* InitializeReadOnlyArtifacts();
  void ClearReadOnlyArtifacts();

#ifdef V8_ENABLE_SANDBOX
  Sandbox* sandbox() { return sandbox_; }

  CodePointerTable* code_pointer_table() { return &code_pointer_table_; }

  MemoryChunkMetadata** metadata_pointer_table() {
    return metadata_pointer_table_;
  }
#endif  // V8_ENABLE_SANDBOX

#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable* js_dispatch_table() { return &js_dispatch_table_; }
#endif  // V8_ENABLE_LEAPTIERING

  void SetupReadOnlyHeap(Isolate* isolate,
                         SnapshotData* read_only_snapshot_data,
                         bool can_rehash);
  void AddIsolate(Isolate* isolate);
  void RemoveIsolate(Isolate* isolate);

 private:
  friend class base::LeakyObject<IsolateGroup>;
  friend class PoolTest;

  // Unless you manually create a new isolate group, all isolates in a process
  // are in the same isolate group and share process-wide resources from
  // that default group.
  static IsolateGroup* default_isolate_group_;

  IsolateGroup();
  ~IsolateGroup();
  IsolateGroup(const IsolateGroup&) = delete;
  IsolateGroup& operator=(const IsolateGroup&) = delete;

  V8_INLINE static IsolateGroup* GetDefault() { return default_isolate_group_; }

  // Only used for testing.
  static void ReleaseDefault();

#ifdef V8_ENABLE_SANDBOX
  void Initialize(bool process_wide, Sandbox* sandbox);
#else   // V8_ENABLE_SANDBOX
  void Initialize(bool process_wide);
#endif  // V8_ENABLE_SANDBOX

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  static IsolateGroup* current_non_inlined();
  static void set_current_non_inlined(IsolateGroup* group);
#endif

  std::atomic<int> reference_count_{1};
  int isolate_count_{0};
  v8::PageAllocator* page_allocator_ = nullptr;

#ifdef V8_COMPRESS_POINTERS
  VirtualMemoryCage* trusted_pointer_compression_cage_ = nullptr;
  VirtualMemoryCage* pointer_compression_cage_ = nullptr;
  VirtualMemoryCage reservation_;
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  thread_local static IsolateGroup* current_;
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

  base::OnceType init_code_range_ = V8_ONCE_INIT;
  std::unique_ptr<CodeRange> code_range_;
  Address external_ref_table_[ExternalReferenceTable::kSizeIsolateIndependent] =
      {0};

  bool process_wide_;

  // Mutex used to synchronize adding and removing of isolates to this group. It
  // is also used to ensure that ReadOnlyArtifacts creation is only done once.
  base::Mutex mutex_;
  std::unique_ptr<ReadOnlyArtifacts> read_only_artifacts_;
  ReadOnlyHeap* shared_read_only_heap_ = nullptr;
  Isolate* shared_space_isolate_ = nullptr;

#ifdef V8_ENABLE_SANDBOX
  Sandbox* sandbox_ = nullptr;
  CodePointerTable code_pointer_table_;
  MemoryChunkMetadata*
      metadata_pointer_table_[MemoryChunkConstants::kMetadataPointerTableSize] =
          {nullptr};
#endif  // V8_ENABLE_SANDBOX

#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable js_dispatch_table_;
#endif  // V8_ENABLE_LEAPTIERING
};

}  // namespace internal
}  // namespace v8

#endif  // V8_INIT_ISOLATE_GROUP_H_

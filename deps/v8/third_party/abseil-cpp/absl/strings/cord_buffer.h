// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: cord_buffer.h
// -----------------------------------------------------------------------------
//
// This file defines an `absl::CordBuffer` data structure to hold data for
// eventual inclusion within an existing `Cord` data structure. Cord buffers are
// useful for building large Cords that may require custom allocation of its
// associated memory.
//
#ifndef ABSL_STRINGS_CORD_BUFFER_H_
#define ABSL_STRINGS_CORD_BUFFER_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/numeric/bits.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class Cord;
class CordBufferTestPeer;

// CordBuffer
//
// CordBuffer manages memory buffers for purposes such as zero-copy APIs as well
// as applications building cords with large data requiring granular control
// over the allocation and size of cord data. For example, a function creating
// a cord of random data could use a CordBuffer as follows:
//
//   absl::Cord CreateRandomCord(size_t length) {
//     absl::Cord cord;
//     while (length > 0) {
//       CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(length);
//       absl::Span<char> data = buffer.available_up_to(length);
//       FillRandomValues(data.data(), data.size());
//       buffer.IncreaseLengthBy(data.size());
//       cord.Append(std::move(buffer));
//       length -= data.size();
//     }
//     return cord;
//   }
//
// CordBuffer instances are by default limited to a capacity of `kDefaultLimit`
// bytes. `kDefaultLimit` is currently just under 4KiB, but this default may
// change in the future and/or for specific architectures. The default limit is
// aimed to provide a good trade-off between performance and memory overhead.
// Smaller buffers typically incur more compute cost while larger buffers are
// more CPU efficient but create significant memory overhead because of such
// allocations being less granular. Using larger buffers may also increase the
// risk of memory fragmentation.
//
// Applications create a buffer using one of the `CreateWithDefaultLimit()` or
// `CreateWithCustomLimit()` methods. The returned instance will have a non-zero
// capacity and a zero length. Applications use the `data()` method to set the
// contents of the managed memory, and once done filling the buffer, use the
// `IncreaseLengthBy()` or 'SetLength()' method to specify the length of the
// initialized data before adding the buffer to a Cord.
//
// The `CreateWithCustomLimit()` method is intended for applications needing
// larger buffers than the default memory limit, allowing the allocation of up
// to a capacity of `kCustomLimit` bytes minus some minimum internal overhead.
// The usage of `CreateWithCustomLimit()` should be limited to only those use
// cases where the distribution of the input is relatively well known, and/or
// where the trade-off between the efficiency gains outweigh the risk of memory
// fragmentation. See the documentation for `CreateWithCustomLimit()` for more
// information on using larger custom limits.
//
// The capacity of a `CordBuffer` returned by one of the `Create` methods may
// be larger than the requested capacity due to rounding, alignment and
// granularity of the memory allocator. Applications should use the `capacity`
// method to obtain the effective capacity of the returned instance as
// demonstrated in the provided example above.
//
// CordBuffer is a move-only class. All references into the managed memory are
// invalidated when an instance is moved into either another CordBuffer instance
// or a Cord. Writing to a location obtained by a previous call to `data()`
// after an instance was moved will lead to undefined behavior.
//
// A `moved from` CordBuffer instance will have a valid, but empty state.
// CordBuffer is thread compatible.
class CordBuffer {
 public:
  // kDefaultLimit
  //
  // Default capacity limits of allocated CordBuffers.
  // See the class comments for more information on allocation limits.
  static constexpr size_t kDefaultLimit = cord_internal::kMaxFlatLength;

  // kCustomLimit
  //
  // Maximum size for CreateWithCustomLimit() allocated buffers.
  // Note that the effective capacity may be slightly less
  // because of internal overhead of internal cord buffers.
  static constexpr size_t kCustomLimit = 64U << 10;

  // Constructors, Destructors and Assignment Operators

  // Creates an empty CordBuffer.
  CordBuffer() = default;

  // Destroys this CordBuffer instance and, if not empty, releases any memory
  // managed by this instance, invalidating previously returned references.
  ~CordBuffer();

  // CordBuffer is move-only
  CordBuffer(CordBuffer&& rhs) noexcept;
  CordBuffer& operator=(CordBuffer&&) noexcept;
  CordBuffer(const CordBuffer&) = delete;
  CordBuffer& operator=(const CordBuffer&) = delete;

  // CordBuffer::MaximumPayload()
  //
  // Returns the guaranteed maximum payload for a CordBuffer returned by the
  // `CreateWithDefaultLimit()` method. While small, each internal buffer inside
  // a Cord incurs an overhead to manage the length, type and reference count
  // for the buffer managed inside the cord tree. Applications can use this
  // method to get approximate number of buffers required for a given byte
  // size, etc.
  //
  // For example:
  //   const size_t payload = absl::CordBuffer::MaximumPayload();
  //   const size_t buffer_count = (total_size + payload - 1) / payload;
  //   buffers.reserve(buffer_count);
  static constexpr size_t MaximumPayload();

  // Overload to the above `MaximumPayload()` except that it returns the
  // maximum payload for a CordBuffer returned by the `CreateWithCustomLimit()`
  // method given the provided `block_size`.
  static constexpr size_t MaximumPayload(size_t block_size);

  // CordBuffer::CreateWithDefaultLimit()
  //
  // Creates a CordBuffer instance of the desired `capacity`, capped at the
  // default limit `kDefaultLimit`. The returned buffer has a guaranteed
  // capacity of at least `min(kDefaultLimit, capacity)`. See the class comments
  // for more information on buffer capacities and intended usage.
  static CordBuffer CreateWithDefaultLimit(size_t capacity);

  // CordBuffer::CreateWithCustomLimit()
  //
  // Creates a CordBuffer instance of the desired `capacity` rounded to an
  // appropriate power of 2 size less than, or equal to `block_size`.
  // Requires `block_size` to be a power of 2.
  //
  // If `capacity` is less than or equal to `kDefaultLimit`, then this method
  // behaves identical to `CreateWithDefaultLimit`, which means that the caller
  // is guaranteed to get a buffer of at least the requested capacity.
  //
  // If `capacity` is greater than or equal to `block_size`, then this method
  // returns a buffer with an `allocated size` of `block_size` bytes. Otherwise,
  // this methods returns a buffer with a suitable smaller power of 2 block size
  // to satisfy the request. The actual size depends on a number of factors, and
  // is typically (but not necessarily) the highest or second highest power of 2
  // value less than or equal to `capacity`.
  //
  // The 'allocated size' includes a small amount of overhead required for
  // internal state, which is currently 13 bytes on 64-bit platforms. For
  // example: a buffer created with `block_size` and `capacity' set to 8KiB
  // will have an allocated size of 8KiB, and an effective internal `capacity`
  // of 8KiB - 13 = 8179 bytes.
  //
  // To demonstrate this in practice, let's assume we want to read data from
  // somewhat larger files using approximately 64KiB buffers:
  //
  //   absl::Cord ReadFromFile(int fd, size_t n) {
  //     absl::Cord cord;
  //     while (n > 0) {
  //       CordBuffer buffer = CordBuffer::CreateWithCustomLimit(64 << 10, n);
  //       absl::Span<char> data = buffer.available_up_to(n);
  //       ReadFileDataOrDie(fd, data.data(), data.size());
  //       buffer.IncreaseLengthBy(data.size());
  //       cord.Append(std::move(buffer));
  //       n -= data.size();
  //     }
  //     return cord;
  //   }
  //
  // If we'd use this function to read a file of 659KiB, we may get the
  // following pattern of allocated cord buffer sizes:
  //
  //   CreateWithCustomLimit(64KiB, 674816) --> ~64KiB (65523)
  //   CreateWithCustomLimit(64KiB, 674816) --> ~64KiB (65523)
  //   ...
  //   CreateWithCustomLimit(64KiB,  19586) --> ~16KiB (16371)
  //   CreateWithCustomLimit(64KiB,   3215) -->   3215 (at least 3215)
  //
  // The reason the method returns a 16K buffer instead of a roughly 19K buffer
  // is to reduce memory overhead and fragmentation risks. Using carefully
  // chosen power of 2 values reduces the entropy of allocated memory sizes.
  //
  // Additionally, let's assume we'd use the above function on files that are
  // generally smaller than 64K. If we'd use 'precise' sized buffers for such
  // files, than we'd get a very wide distribution of allocated memory sizes
  // rounded to 4K page sizes, and we'd end up with a lot of unused capacity.
  //
  // In general, application should only use custom sizes if the data they are
  // consuming or storing is expected to be many times the chosen block size,
  // and be based on objective data and performance metrics. For example, a
  // compress function may work faster and consume less CPU when using larger
  // buffers. Such an application should pick a size offering a reasonable
  // trade-off between expected data size, compute savings with larger buffers,
  // and the cost or fragmentation effect of larger buffers.
  // Applications must pick a reasonable spot on that curve, and make sure their
  // data meets their expectations in size distributions such as "mostly large".
  static CordBuffer CreateWithCustomLimit(size_t block_size, size_t capacity);

  // CordBuffer::available()
  //
  // Returns the span delineating the available capacity in this buffer
  // which is defined as `{ data() + length(), capacity() - length() }`.
  absl::Span<char> available();

  // CordBuffer::available_up_to()
  //
  // Returns the span delineating the available capacity in this buffer limited
  // to `size` bytes. This is equivalent to `available().subspan(0, size)`.
  absl::Span<char> available_up_to(size_t size);

  // CordBuffer::data()
  //
  // Returns a non-null reference to the data managed by this instance.
  // Applications are allowed to write up to `capacity` bytes of instance data.
  // CordBuffer data is uninitialized by default. Reading data from an instance
  // that has not yet been initialized will lead to undefined behavior.
  char* data();
  const char* data() const;

  // CordBuffer::length()
  //
  // Returns the length of this instance. The default length of a CordBuffer is
  // 0, indicating an 'empty' CordBuffer. Applications must specify the length
  // of the data in a CordBuffer before adding it to a Cord.
  size_t length() const;

  // CordBuffer::capacity()
  //
  // Returns the capacity of this instance. All instances have a non-zero
  // capacity: default and `moved from` instances have a small internal buffer.
  size_t capacity() const;

  // CordBuffer::IncreaseLengthBy()
  //
  // Increases the length of this buffer by the specified 'n' bytes.
  // Applications must make sure all data in this buffer up to the new length
  // has been initialized before adding a CordBuffer to a Cord: failure to do so
  // will lead to undefined behavior.  Requires `length() + n <= capacity()`.
  // Typically, applications will use 'available_up_to()` to get a span of the
  // desired capacity, and use `span.size()` to increase the length as in:
  //   absl::Span<char> span = buffer.available_up_to(desired);
  //   buffer.IncreaseLengthBy(span.size());
  //   memcpy(span.data(), src, span.size());
  //   etc...
  void IncreaseLengthBy(size_t n);

  // CordBuffer::SetLength()
  //
  // Sets the data length of this instance. Applications must make sure all data
  // of the specified length has been initialized before adding a CordBuffer to
  // a Cord: failure to do so will lead to undefined behavior.
  // Setting the length to a small value or zero does not release any memory
  // held by this CordBuffer instance. Requires `length <= capacity()`.
  // Applications should preferably use the `IncreaseLengthBy()` method above
  // in combination with the 'available()` or `available_up_to()` methods.
  void SetLength(size_t length);

 private:
  // Make sure we don't accidentally over promise.
  static_assert(kCustomLimit <= cord_internal::kMaxLargeFlatSize, "");

  // Assume the cost of an 'uprounded' allocation to CeilPow2(size) versus
  // the cost of allocating at least 1 extra flat <= 4KB:
  // - Flat overhead = 13 bytes
  // - Btree amortized cost / node =~ 13 bytes
  // - 64 byte granularity of tcmalloc at 4K =~ 32 byte average
  // CPU cost and efficiency requires we should at least 'save' something by
  // splitting, as a poor man's measure, we say the slop needs to be
  // at least double the cost offset to make it worth splitting: ~128 bytes.
  static constexpr size_t kMaxPageSlop = 128;

  // Overhead for allocation a flat.
  static constexpr size_t kOverhead = cord_internal::kFlatOverhead;

  using CordRepFlat = cord_internal::CordRepFlat;

  // `Rep` is the internal data representation of a CordBuffer. The internal
  // representation has an internal small size optimization similar to
  // std::string (SSO).
  struct Rep {
    // Inline SSO size of a CordBuffer
    static constexpr size_t kInlineCapacity = sizeof(intptr_t) * 2 - 1;

    // Creates a default instance with kInlineCapacity.
    Rep() : short_rep{} {}

    // Creates an instance managing an allocated non zero CordRep.
    explicit Rep(cord_internal::CordRepFlat* rep) : long_rep{rep} {
      assert(rep != nullptr);
    }

    // Returns true if this instance manages the SSO internal buffer.
    bool is_short() const {
      constexpr size_t offset = offsetof(Short, raw_size);
      return (reinterpret_cast<const char*>(this)[offset] & 1) != 0;
    }

    // Returns the available area of the internal SSO data
    absl::Span<char> short_available() {
      const size_t length = short_length();
      return absl::Span<char>(short_rep.data + length,
                              kInlineCapacity - length);
    }

    // Returns the available area of the internal SSO data
    absl::Span<char> long_available() const {
      assert(!is_short());
      const size_t length = long_rep.rep->length;
      return absl::Span<char>(long_rep.rep->Data() + length,
                              long_rep.rep->Capacity() - length);
    }

    // Returns the length of the internal SSO data.
    size_t short_length() const {
      assert(is_short());
      return static_cast<size_t>(short_rep.raw_size >> 1);
    }

    // Sets the length of the internal SSO data.
    // Disregards any previously set CordRep instance.
    void set_short_length(size_t length) {
      short_rep.raw_size = static_cast<char>((length << 1) + 1);
    }

    // Adds `n` to the current short length.
    void add_short_length(size_t n) {
      assert(is_short());
      short_rep.raw_size += static_cast<char>(n << 1);
    }

    // Returns reference to the internal SSO data buffer.
    char* data() {
      assert(is_short());
      return short_rep.data;
    }
    const char* data() const {
      assert(is_short());
      return short_rep.data;
    }

    // Returns a pointer the external CordRep managed by this instance.
    cord_internal::CordRepFlat* rep() const {
      assert(!is_short());
      return long_rep.rep;
    }

    // The internal representation takes advantage of the fact that allocated
    // memory is always on an even address, and uses the least significant bit
    // of the first or last byte (depending on endianness) as the inline size
    // indicator overlapping with the least significant byte of the CordRep*.
#if defined(ABSL_IS_BIG_ENDIAN)
    struct Long {
      explicit Long(cord_internal::CordRepFlat* rep_arg) : rep(rep_arg) {}
      void* padding;
      cord_internal::CordRepFlat* rep;
    };
    struct Short {
      char data[sizeof(Long) - 1];
      char raw_size = 1;
    };
#else
    struct Long {
      explicit Long(cord_internal::CordRepFlat* rep_arg) : rep(rep_arg) {}
      cord_internal::CordRepFlat* rep;
      void* padding;
    };
    struct Short {
      char raw_size = 1;
      char data[sizeof(Long) - 1];
    };
#endif

    union {
      Long long_rep;
      Short short_rep;
    };
  };

  // Power2 functions
  static bool IsPow2(size_t size) { return absl::has_single_bit(size); }
  static size_t Log2Floor(size_t size) {
    return static_cast<size_t>(absl::bit_width(size) - 1);
  }
  static size_t Log2Ceil(size_t size) {
    return static_cast<size_t>(absl::bit_width(size - 1));
  }

  // Implementation of `CreateWithCustomLimit()`.
  // This implementation allows for future memory allocation hints to
  // be passed down into the CordRepFlat allocation function.
  template <typename... AllocationHints>
  static CordBuffer CreateWithCustomLimitImpl(size_t block_size,
                                              size_t capacity,
                                              AllocationHints... hints);

  // Consumes the value contained in this instance and resets the instance.
  // This method returns a non-null Cordrep* if the current instances manages a
  // CordRep*, and resets the instance to an empty SSO instance. If the current
  // instance is an SSO instance, then this method returns nullptr and sets
  // `short_value` to the inlined data value. In either case, the current
  // instance length is reset to zero.
  // This method is intended to be used by Cord internal functions only.
  cord_internal::CordRep* ConsumeValue(absl::string_view& short_value) {
    cord_internal::CordRep* rep = nullptr;
    if (rep_.is_short()) {
      short_value = absl::string_view(rep_.data(), rep_.short_length());
    } else {
      rep = rep_.rep();
    }
    rep_.set_short_length(0);
    return rep;
  }

  // Internal constructor.
  explicit CordBuffer(cord_internal::CordRepFlat* rep) : rep_(rep) {
    assert(rep != nullptr);
  }

  Rep rep_;

  friend class Cord;
  friend class CordBufferTestPeer;
};

inline constexpr size_t CordBuffer::MaximumPayload() {
  return cord_internal::kMaxFlatLength;
}

inline constexpr size_t CordBuffer::MaximumPayload(size_t block_size) {
  return (std::min)(kCustomLimit, block_size) - cord_internal::kFlatOverhead;
}

inline CordBuffer CordBuffer::CreateWithDefaultLimit(size_t capacity) {
  if (capacity > Rep::kInlineCapacity) {
    auto* rep = cord_internal::CordRepFlat::New(capacity);
    rep->length = 0;
    return CordBuffer(rep);
  }
  return CordBuffer();
}

template <typename... AllocationHints>
inline CordBuffer CordBuffer::CreateWithCustomLimitImpl(
    size_t block_size, size_t capacity, AllocationHints... hints) {
  assert(IsPow2(block_size));
  capacity = (std::min)(capacity, kCustomLimit);
  block_size = (std::min)(block_size, kCustomLimit);
  if (capacity + kOverhead >= block_size) {
    capacity = block_size;
  } else if (capacity <= kDefaultLimit) {
    capacity = capacity + kOverhead;
  } else if (!IsPow2(capacity)) {
    // Check if rounded up to next power 2 is a good enough fit
    // with limited waste making it an acceptable direct fit.
    const size_t rounded_up = size_t{1} << Log2Ceil(capacity);
    const size_t slop = rounded_up - capacity;
    if (slop >= kOverhead && slop <= kMaxPageSlop + kOverhead) {
      capacity = rounded_up;
    } else {
      // Round down to highest power of 2 <= capacity.
      // Consider a more aggressive step down if that may reduce the
      // risk of fragmentation where 'people are holding it wrong'.
      const size_t rounded_down = size_t{1} << Log2Floor(capacity);
      capacity = rounded_down;
    }
  }
  const size_t length = capacity - kOverhead;
  auto* rep = CordRepFlat::New(CordRepFlat::Large(), length, hints...);
  rep->length = 0;
  return CordBuffer(rep);
}

inline CordBuffer CordBuffer::CreateWithCustomLimit(size_t block_size,
                                                    size_t capacity) {
  return CreateWithCustomLimitImpl(block_size, capacity);
}

inline CordBuffer::~CordBuffer() {
  if (!rep_.is_short()) {
    cord_internal::CordRepFlat::Delete(rep_.rep());
  }
}

inline CordBuffer::CordBuffer(CordBuffer&& rhs) noexcept : rep_(rhs.rep_) {
  rhs.rep_.set_short_length(0);
}

inline CordBuffer& CordBuffer::operator=(CordBuffer&& rhs) noexcept {
  if (!rep_.is_short()) cord_internal::CordRepFlat::Delete(rep_.rep());
  rep_ = rhs.rep_;
  rhs.rep_.set_short_length(0);
  return *this;
}

inline absl::Span<char> CordBuffer::available() {
  return rep_.is_short() ? rep_.short_available() : rep_.long_available();
}

inline absl::Span<char> CordBuffer::available_up_to(size_t size) {
  return available().subspan(0, size);
}

inline char* CordBuffer::data() {
  return rep_.is_short() ? rep_.data() : rep_.rep()->Data();
}

inline const char* CordBuffer::data() const {
  return rep_.is_short() ? rep_.data() : rep_.rep()->Data();
}

inline size_t CordBuffer::capacity() const {
  return rep_.is_short() ? Rep::kInlineCapacity : rep_.rep()->Capacity();
}

inline size_t CordBuffer::length() const {
  return rep_.is_short() ? rep_.short_length() : rep_.rep()->length;
}

inline void CordBuffer::SetLength(size_t length) {
  ABSL_HARDENING_ASSERT(length <= capacity());
  if (rep_.is_short()) {
    rep_.set_short_length(length);
  } else {
    rep_.rep()->length = length;
  }
}

inline void CordBuffer::IncreaseLengthBy(size_t n) {
  ABSL_HARDENING_ASSERT(n <= capacity() && length() + n <= capacity());
  if (rep_.is_short()) {
    rep_.add_short_length(n);
  } else {
    rep_.rep()->length += n;
  }
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CORD_BUFFER_H_

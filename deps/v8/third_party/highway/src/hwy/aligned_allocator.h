// Copyright 2020 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_ALIGNED_ALLOCATOR_H_
#define HIGHWAY_HWY_ALIGNED_ALLOCATOR_H_

// Memory allocator with support for alignment and offsets.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "hwy/base.h"
#include "hwy/per_target.h"

namespace hwy {

// Minimum alignment of allocated memory for use in HWY_ASSUME_ALIGNED, which
// requires a literal. To prevent false sharing, this should be at least the
// L1 cache line size, usually 64 bytes. However, Intel's L2 prefetchers may
// access pairs of lines, and M1 L2 and POWER8 lines are also 128 bytes.
#define HWY_ALIGNMENT 128

template <typename T>
HWY_API constexpr bool IsAligned(T* ptr, size_t align = HWY_ALIGNMENT) {
  return reinterpret_cast<uintptr_t>(ptr) % align == 0;
}

// Pointers to functions equivalent to malloc/free with an opaque void* passed
// to them.
using AllocPtr = void* (*)(void* opaque, size_t bytes);
using FreePtr = void (*)(void* opaque, void* memory);

// Returns null or a pointer to at least `payload_size` (which can be zero)
// bytes of newly allocated memory, aligned to the larger of HWY_ALIGNMENT and
// the vector size. Calls `alloc` with the passed `opaque` pointer to obtain
// memory or malloc() if it is null.
HWY_DLLEXPORT void* AllocateAlignedBytes(size_t payload_size,
                                         AllocPtr alloc_ptr = nullptr,
                                         void* opaque_ptr = nullptr);

// Frees all memory. No effect if `aligned_pointer` == nullptr, otherwise it
// must have been returned from a previous call to `AllocateAlignedBytes`.
// Calls `free_ptr` with the passed `opaque_ptr` pointer to free the memory; if
// `free_ptr` function is null, uses the default free().
HWY_DLLEXPORT void FreeAlignedBytes(const void* aligned_pointer,
                                    FreePtr free_ptr, void* opaque_ptr);

// Class that deletes the aligned pointer passed to operator() calling the
// destructor before freeing the pointer. This is equivalent to the
// std::default_delete but for aligned objects. For a similar deleter equivalent
// to free() for aligned memory see AlignedFreer().
class AlignedDeleter {
 public:
  AlignedDeleter() : free_(nullptr), opaque_ptr_(nullptr) {}
  AlignedDeleter(FreePtr free_ptr, void* opaque_ptr)
      : free_(free_ptr), opaque_ptr_(opaque_ptr) {}

  template <typename T>
  void operator()(T* aligned_pointer) const {
    return DeleteAlignedArray(aligned_pointer, free_, opaque_ptr_,
                              TypedArrayDeleter<T>);
  }

 private:
  template <typename T>
  static void TypedArrayDeleter(void* ptr, size_t size_in_bytes) {
    size_t elems = size_in_bytes / sizeof(T);
    for (size_t i = 0; i < elems; i++) {
      // Explicitly call the destructor on each element.
      (static_cast<T*>(ptr) + i)->~T();
    }
  }

  // Function prototype that calls the destructor for each element in a typed
  // array. TypeArrayDeleter<T> would match this prototype.
  using ArrayDeleter = void (*)(void* t_ptr, size_t t_size);

  HWY_DLLEXPORT static void DeleteAlignedArray(void* aligned_pointer,
                                               FreePtr free_ptr,
                                               void* opaque_ptr,
                                               ArrayDeleter deleter);

  FreePtr free_;
  void* opaque_ptr_;
};

// Unique pointer to T with custom aligned deleter. This can be a single
// element U or an array of element if T is a U[]. The custom aligned deleter
// will call the destructor on U or each element of a U[] in the array case.
template <typename T>
using AlignedUniquePtr = std::unique_ptr<T, AlignedDeleter>;

// Aligned memory equivalent of make_unique<T> using the custom allocators
// alloc/free with the passed `opaque` pointer. This function calls the
// constructor with the passed Args... and calls the destructor of the object
// when the AlignedUniquePtr is destroyed.
template <typename T, typename... Args>
AlignedUniquePtr<T> MakeUniqueAlignedWithAlloc(AllocPtr alloc, FreePtr free,
                                               void* opaque, Args&&... args) {
  T* ptr = static_cast<T*>(AllocateAlignedBytes(sizeof(T), alloc, opaque));
  return AlignedUniquePtr<T>(new (ptr) T(std::forward<Args>(args)...),
                             AlignedDeleter(free, opaque));
}

// Similar to MakeUniqueAlignedWithAlloc but using the default alloc/free
// functions.
template <typename T, typename... Args>
AlignedUniquePtr<T> MakeUniqueAligned(Args&&... args) {
  T* ptr = static_cast<T*>(AllocateAlignedBytes(sizeof(T)));
  return AlignedUniquePtr<T>(new (ptr) T(std::forward<Args>(args)...),
                             AlignedDeleter());
}

template <class T>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() = default;

  template <class V>
  explicit AlignedAllocator(const AlignedAllocator<V>&) noexcept {}

  template <class V>
  value_type* allocate(V n) {
    static_assert(std::is_integral<V>::value,
                  "AlignedAllocator only supports integer types");
    static_assert(sizeof(V) <= sizeof(std::size_t),
                  "V n must be smaller or equal size_t to avoid overflow");
    return static_cast<value_type*>(
        AllocateAlignedBytes(static_cast<std::size_t>(n) * sizeof(value_type)));
  }

  template <class V>
  void deallocate(value_type* p, HWY_MAYBE_UNUSED V n) {
    return FreeAlignedBytes(p, nullptr, nullptr);
  }
};

template <class T, class V>
constexpr bool operator==(const AlignedAllocator<T>&,
                          const AlignedAllocator<V>&) noexcept {
  return true;
}

template <class T, class V>
constexpr bool operator!=(const AlignedAllocator<T>&,
                          const AlignedAllocator<V>&) noexcept {
  return false;
}

template <class T>
using AlignedVector = std::vector<T, AlignedAllocator<T>>;

// Helpers for array allocators (avoids overflow)
namespace detail {

// Returns x such that 1u << x == n (if n is a power of two).
static inline constexpr size_t ShiftCount(size_t n) {
  return (n <= 1) ? 0 : 1 + ShiftCount(n / 2);
}

template <typename T>
T* AllocateAlignedItems(size_t items, AllocPtr alloc_ptr, void* opaque_ptr) {
  constexpr size_t kSize = sizeof(T);

  constexpr bool kIsPow2 = (kSize & (kSize - 1)) == 0;
  constexpr size_t kBits = ShiftCount(kSize);
  static_assert(!kIsPow2 || (1ull << kBits) == kSize, "ShiftCount has a bug");

  const size_t bytes = kIsPow2 ? items << kBits : items * kSize;
  const size_t check = kIsPow2 ? bytes >> kBits : bytes / kSize;
  if (check != items) {
    return nullptr;  // overflowed
  }
  return static_cast<T*>(AllocateAlignedBytes(bytes, alloc_ptr, opaque_ptr));
}

}  // namespace detail

// Aligned memory equivalent of make_unique<T[]> for array types using the
// custom allocators alloc/free. This function calls the constructor with the
// passed Args... on every created item. The destructor of each element will be
// called when the AlignedUniquePtr is destroyed.
template <typename T, typename... Args>
AlignedUniquePtr<T[]> MakeUniqueAlignedArrayWithAlloc(
    size_t items, AllocPtr alloc, FreePtr free, void* opaque, Args&&... args) {
  T* ptr = detail::AllocateAlignedItems<T>(items, alloc, opaque);
  if (ptr != nullptr) {
    for (size_t i = 0; i < items; i++) {
      new (ptr + i) T(std::forward<Args>(args)...);
    }
  }
  return AlignedUniquePtr<T[]>(ptr, AlignedDeleter(free, opaque));
}

template <typename T, typename... Args>
AlignedUniquePtr<T[]> MakeUniqueAlignedArray(size_t items, Args&&... args) {
  return MakeUniqueAlignedArrayWithAlloc<T, Args...>(
      items, nullptr, nullptr, nullptr, std::forward<Args>(args)...);
}

// Custom deleter for std::unique_ptr equivalent to using free() as a deleter
// but for aligned memory.
class AlignedFreer {
 public:
  // Pass address of this to ctor to skip deleting externally-owned memory.
  static void DoNothing(void* /*opaque*/, void* /*aligned_pointer*/) {}

  AlignedFreer() : free_(nullptr), opaque_ptr_(nullptr) {}
  AlignedFreer(FreePtr free_ptr, void* opaque_ptr)
      : free_(free_ptr), opaque_ptr_(opaque_ptr) {}

  template <typename T>
  void operator()(T* aligned_pointer) const {
    // TODO(deymo): assert that we are using a POD type T.
    FreeAlignedBytes(aligned_pointer, free_, opaque_ptr_);
  }

 private:
  FreePtr free_;
  void* opaque_ptr_;
};

// Unique pointer to single POD, or (if T is U[]) an array of POD. For non POD
// data use AlignedUniquePtr.
template <typename T>
using AlignedFreeUniquePtr = std::unique_ptr<T, AlignedFreer>;

// Allocate an aligned and uninitialized array of POD values as a unique_ptr.
// Upon destruction of the unique_ptr the aligned array will be freed.
template <typename T>
AlignedFreeUniquePtr<T[]> AllocateAligned(const size_t items, AllocPtr alloc,
                                          FreePtr free, void* opaque) {
  return AlignedFreeUniquePtr<T[]>(
      detail::AllocateAlignedItems<T>(items, alloc, opaque),
      AlignedFreer(free, opaque));
}

// Same as previous AllocateAligned(), using default allocate/free functions.
template <typename T>
AlignedFreeUniquePtr<T[]> AllocateAligned(const size_t items) {
  return AllocateAligned<T>(items, nullptr, nullptr, nullptr);
}

// A simple span containing data and size of data.
template <typename T>
class Span {
 public:
  Span() = default;
  Span(T* data, size_t size) : size_(size), data_(data) {}
  template <typename U>
  Span(U u) : Span(u.data(), u.size()) {}
  Span(std::initializer_list<const T> v) : Span(v.begin(), v.size()) {}

  // Copies the contents of the initializer list to the span.
  Span<T>& operator=(std::initializer_list<const T> v) {
    HWY_DASSERT(size_ == v.size());
    CopyBytes(v.begin(), data_, sizeof(T) * std::min(size_, v.size()));
    return *this;
  }

  // Returns the size of the contained data.
  size_t size() const { return size_; }

  // Returns a pointer to the contained data.
  T* data() { return data_; }
  T* data() const { return data_; }

  // Returns the element at index.
  T& operator[](size_t index) const { return data_[index]; }

  // Returns an iterator pointing to the first element of this span.
  T* begin() { return data_; }

  // Returns a const iterator pointing to the first element of this span.
  constexpr const T* cbegin() const { return data_; }

  // Returns an iterator pointing just beyond the last element at the
  // end of this span.
  T* end() { return data_ + size_; }

  // Returns a const iterator pointing just beyond the last element at the
  // end of this span.
  constexpr const T* cend() const { return data_ + size_; }

 private:
  size_t size_ = 0;
  T* data_ = nullptr;
};

// A multi dimensional array containing an aligned buffer.
//
// To maintain alignment, the innermost dimension will be padded to ensure all
// innermost arrays are aligned.
template <typename T, size_t axes>
class AlignedNDArray {
  static_assert(std::is_trivial<T>::value,
                "AlignedNDArray can only contain trivial types");

 public:
  AlignedNDArray(AlignedNDArray&& other) = default;
  AlignedNDArray& operator=(AlignedNDArray&& other) = default;

  // Constructs an array of the provided shape and fills it with zeros.
  explicit AlignedNDArray(std::array<size_t, axes> shape) : shape_(shape) {
    sizes_ = ComputeSizes(shape_);
    memory_shape_ = shape_;
    // Round the innermost dimension up to the number of bytes available for
    // SIMD operations on this architecture to make sure that each innermost
    // array is aligned from the first element.
    memory_shape_[axes - 1] = RoundUpTo(memory_shape_[axes - 1], VectorBytes());
    memory_sizes_ = ComputeSizes(memory_shape_);
    buffer_ = hwy::AllocateAligned<T>(memory_size());
    hwy::ZeroBytes(buffer_.get(), memory_size() * sizeof(T));
  }

  // Returns a span containing the innermost array at the provided indices.
  Span<T> operator[](std::array<const size_t, axes - 1> indices) {
    return Span<T>(buffer_.get() + Offset(indices), sizes_[indices.size()]);
  }

  // Returns a const span containing the innermost array at the provided
  // indices.
  Span<const T> operator[](std::array<const size_t, axes - 1> indices) const {
    return Span<const T>(buffer_.get() + Offset(indices),
                         sizes_[indices.size()]);
  }

  // Returns the shape of the array, which might be smaller than the allocated
  // buffer after padding the last axis to alignment.
  const std::array<size_t, axes>& shape() const { return shape_; }

  // Returns the shape of the allocated buffer, which might be larger than the
  // used size of the array after padding to alignment.
  const std::array<size_t, axes>& memory_shape() const { return memory_shape_; }

  // Returns the size of the array, which might be smaller than the allocated
  // buffer after padding the last axis to alignment.
  size_t size() const { return sizes_[0]; }

  // Returns the size of the allocated buffer, which might be larger than the
  // used size of the array after padding to alignment.
  size_t memory_size() const { return memory_sizes_[0]; }

  // Returns a pointer to the allocated buffer.
  T* data() { return buffer_.get(); }

  // Returns a const pointer to the buffer.
  const T* data() const { return buffer_.get(); }

  // Truncates the array by updating its shape.
  //
  // The new shape must be equal to or less than the old shape in all axes.
  //
  // Doesn't modify underlying memory.
  void truncate(const std::array<size_t, axes>& new_shape) {
#if HWY_IS_DEBUG_BUILD
    for (size_t axis_index = 0; axis_index < axes; ++axis_index) {
      HWY_ASSERT(new_shape[axis_index] <= shape_[axis_index]);
    }
#endif
    shape_ = new_shape;
    sizes_ = ComputeSizes(shape_);
  }

 private:
  std::array<size_t, axes> shape_;
  std::array<size_t, axes> memory_shape_;
  std::array<size_t, axes + 1> sizes_;
  std::array<size_t, axes + 1> memory_sizes_;
  hwy::AlignedFreeUniquePtr<T[]> buffer_;

  // Computes offset in the buffer based on the provided indices.
  size_t Offset(std::array<const size_t, axes - 1> indices) const {
    size_t offset = 0;
    size_t shape_index = 0;
    for (const size_t axis_index : indices) {
      offset += memory_sizes_[shape_index + 1] * axis_index;
      shape_index++;
    }
    return offset;
  }

  // Computes the sizes of all sub arrays based on the sizes of each axis.
  //
  // Does this by multiplying the size of each axis with the previous one in
  // reverse order, starting with the conceptual axis of size 1 containing the
  // actual elements in the array.
  static std::array<size_t, axes + 1> ComputeSizes(
      std::array<size_t, axes> shape) {
    std::array<size_t, axes + 1> sizes;
    size_t axis = shape.size();
    sizes[axis] = 1;
    while (axis > 0) {
      --axis;
      sizes[axis] = sizes[axis + 1] * shape[axis];
    }
    return sizes;
  }
};

}  // namespace hwy
#endif  // HIGHWAY_HWY_ALIGNED_ALLOCATOR_H_

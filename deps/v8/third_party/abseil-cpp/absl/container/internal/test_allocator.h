// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_CONTAINER_INTERNAL_TEST_ALLOCATOR_H_
#define ABSL_CONTAINER_INTERNAL_TEST_ALLOCATOR_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// This is a stateful allocator, but the state lives outside of the
// allocator (in whatever test is using the allocator). This is odd
// but helps in tests where the allocator is propagated into nested
// containers - that chain of allocators uses the same state and is
// thus easier to query for aggregate allocation information.
template <typename T>
class CountingAllocator {
 public:
  using Allocator = std::allocator<T>;
  using AllocatorTraits = std::allocator_traits<Allocator>;
  using value_type = typename AllocatorTraits::value_type;
  using pointer = typename AllocatorTraits::pointer;
  using const_pointer = typename AllocatorTraits::const_pointer;
  using size_type = typename AllocatorTraits::size_type;
  using difference_type = typename AllocatorTraits::difference_type;

  CountingAllocator() = default;
  explicit CountingAllocator(int64_t* bytes_used) : bytes_used_(bytes_used) {}
  CountingAllocator(int64_t* bytes_used, int64_t* instance_count)
      : bytes_used_(bytes_used), instance_count_(instance_count) {}

  template <typename U>
  CountingAllocator(const CountingAllocator<U>& x)
      : bytes_used_(x.bytes_used_), instance_count_(x.instance_count_) {}

  pointer allocate(
      size_type n,
      typename AllocatorTraits::const_void_pointer hint = nullptr) {
    Allocator allocator;
    pointer ptr = AllocatorTraits::allocate(allocator, n, hint);
    if (bytes_used_ != nullptr) {
      *bytes_used_ += n * sizeof(T);
    }
    return ptr;
  }

  void deallocate(pointer p, size_type n) {
    Allocator allocator;
    AllocatorTraits::deallocate(allocator, p, n);
    if (bytes_used_ != nullptr) {
      *bytes_used_ -= n * sizeof(T);
    }
  }

  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    Allocator allocator;
    AllocatorTraits::construct(allocator, p, std::forward<Args>(args)...);
    if (instance_count_ != nullptr) {
      *instance_count_ += 1;
    }
  }

  template <typename U>
  void destroy(U* p) {
    Allocator allocator;
    // Ignore GCC warning bug.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
#endif
    AllocatorTraits::destroy(allocator, p);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
    if (instance_count_ != nullptr) {
      *instance_count_ -= 1;
    }
  }

  template <typename U>
  class rebind {
   public:
    using other = CountingAllocator<U>;
  };

  friend bool operator==(const CountingAllocator& a,
                         const CountingAllocator& b) {
    return a.bytes_used_ == b.bytes_used_ &&
           a.instance_count_ == b.instance_count_;
  }

  friend bool operator!=(const CountingAllocator& a,
                         const CountingAllocator& b) {
    return !(a == b);
  }

  int64_t* bytes_used_ = nullptr;
  int64_t* instance_count_ = nullptr;
};

template <typename T>
struct CopyAssignPropagatingCountingAlloc : public CountingAllocator<T> {
  using propagate_on_container_copy_assignment = std::true_type;

  using Base = CountingAllocator<T>;
  using Base::Base;

  template <typename U>
  explicit CopyAssignPropagatingCountingAlloc(
      const CopyAssignPropagatingCountingAlloc<U>& other)
      : Base(other.bytes_used_, other.instance_count_) {}

  template <typename U>
  struct rebind {
    using other = CopyAssignPropagatingCountingAlloc<U>;
  };
};

template <typename T>
struct MoveAssignPropagatingCountingAlloc : public CountingAllocator<T> {
  using propagate_on_container_move_assignment = std::true_type;

  using Base = CountingAllocator<T>;
  using Base::Base;

  template <typename U>
  explicit MoveAssignPropagatingCountingAlloc(
      const MoveAssignPropagatingCountingAlloc<U>& other)
      : Base(other.bytes_used_, other.instance_count_) {}

  template <typename U>
  struct rebind {
    using other = MoveAssignPropagatingCountingAlloc<U>;
  };
};

template <typename T>
struct SwapPropagatingCountingAlloc : public CountingAllocator<T> {
  using propagate_on_container_swap = std::true_type;

  using Base = CountingAllocator<T>;
  using Base::Base;

  template <typename U>
  explicit SwapPropagatingCountingAlloc(
      const SwapPropagatingCountingAlloc<U>& other)
      : Base(other.bytes_used_, other.instance_count_) {}

  template <typename U>
  struct rebind {
    using other = SwapPropagatingCountingAlloc<U>;
  };
};

// Tries to allocate memory at the minimum alignment even when the default
// allocator uses a higher alignment.
template <typename T>
struct MinimumAlignmentAlloc : std::allocator<T> {
  MinimumAlignmentAlloc() = default;

  template <typename U>
  explicit MinimumAlignmentAlloc(const MinimumAlignmentAlloc<U>& /*other*/) {}

  template <class U>
  struct rebind {
    using other = MinimumAlignmentAlloc<U>;
  };

  T* allocate(size_t n) {
    T* ptr = std::allocator<T>::allocate(n + 1);
    char* cptr = reinterpret_cast<char*>(ptr);
    cptr += alignof(T);
    return reinterpret_cast<T*>(cptr);
  }

  void deallocate(T* ptr, size_t n) {
    char* cptr = reinterpret_cast<char*>(ptr);
    cptr -= alignof(T);
    std::allocator<T>::deallocate(reinterpret_cast<T*>(cptr), n + 1);
  }
};

inline bool IsAssertEnabled() {
  // Use an assert with side-effects to figure out if they are actually enabled.
  bool assert_enabled = false;
  assert([&]() {  // NOLINT
    assert_enabled = true;
    return true;
  }());
  return assert_enabled;
}

template <template <class Alloc> class Container>
void TestCopyAssignAllocPropagation() {
  int64_t bytes1 = 0, instances1 = 0, bytes2 = 0, instances2 = 0;
  CopyAssignPropagatingCountingAlloc<int> allocator1(&bytes1, &instances1);
  CopyAssignPropagatingCountingAlloc<int> allocator2(&bytes2, &instances2);

  // Test propagating allocator_type.
  {
    Container<CopyAssignPropagatingCountingAlloc<int>> c1(allocator1);
    Container<CopyAssignPropagatingCountingAlloc<int>> c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_NE(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2 = c1;

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 200);
    EXPECT_EQ(instances2, 0);
  }
  // Test non-propagating allocator_type with different allocators.
  {
    Container<CountingAllocator<int>> c1(allocator1), c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_EQ(c2.get_allocator(), allocator2);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2 = c1;

    EXPECT_EQ(c2.get_allocator(), allocator2);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 100);
  }
  EXPECT_EQ(bytes1, 0);
  EXPECT_EQ(instances1, 0);
  EXPECT_EQ(bytes2, 0);
  EXPECT_EQ(instances2, 0);
}

template <template <class Alloc> class Container>
void TestMoveAssignAllocPropagation() {
  int64_t bytes1 = 0, instances1 = 0, bytes2 = 0, instances2 = 0;
  MoveAssignPropagatingCountingAlloc<int> allocator1(&bytes1, &instances1);
  MoveAssignPropagatingCountingAlloc<int> allocator2(&bytes2, &instances2);

  // Test propagating allocator_type.
  {
    Container<MoveAssignPropagatingCountingAlloc<int>> c1(allocator1);
    Container<MoveAssignPropagatingCountingAlloc<int>> c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_NE(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2 = std::move(c1);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);
  }
  // Test non-propagating allocator_type with equal allocators.
  {
    Container<CountingAllocator<int>> c1(allocator1), c2(allocator1);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2 = std::move(c1);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);
  }
  // Test non-propagating allocator_type with different allocators.
  {
    Container<CountingAllocator<int>> c1(allocator1), c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_NE(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2 = std::move(c1);

    EXPECT_EQ(c2.get_allocator(), allocator2);
    EXPECT_LE(instances1, 100);  // The values in c1 may or may not have been
                                 // destroyed at this point.
    EXPECT_EQ(instances2, 100);
  }
  EXPECT_EQ(bytes1, 0);
  EXPECT_EQ(instances1, 0);
  EXPECT_EQ(bytes2, 0);
  EXPECT_EQ(instances2, 0);
}

template <template <class Alloc> class Container>
void TestSwapAllocPropagation() {
  int64_t bytes1 = 0, instances1 = 0, bytes2 = 0, instances2 = 0;
  SwapPropagatingCountingAlloc<int> allocator1(&bytes1, &instances1);
  SwapPropagatingCountingAlloc<int> allocator2(&bytes2, &instances2);

  // Test propagating allocator_type.
  {
    Container<SwapPropagatingCountingAlloc<int>> c1(allocator1), c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_NE(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2.swap(c1);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);
  }
  // Test non-propagating allocator_type with equal allocators.
  {
    Container<CountingAllocator<int>> c1(allocator1), c2(allocator1);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);

    c2.swap(c1);

    EXPECT_EQ(c2.get_allocator(), allocator1);
    EXPECT_EQ(instances1, 100);
    EXPECT_EQ(instances2, 0);
  }
  // Test non-propagating allocator_type with different allocators.
  {
    Container<CountingAllocator<int>> c1(allocator1), c2(allocator2);

    for (int i = 0; i < 100; ++i) c1.insert(i);

    EXPECT_NE(c1.get_allocator(), c2.get_allocator());
    if (IsAssertEnabled()) {
      EXPECT_DEATH_IF_SUPPORTED(c2.swap(c1), "");
    }
  }
  EXPECT_EQ(bytes1, 0);
  EXPECT_EQ(instances1, 0);
  EXPECT_EQ(bytes2, 0);
  EXPECT_EQ(instances2, 0);
}

template <template <class Alloc> class Container>
void TestAllocPropagation() {
  TestCopyAssignAllocPropagation<Container>();
  TestMoveAssignAllocPropagation<Container>();
  TestSwapAllocPropagation<Container>();
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_TEST_ALLOCATOR_H_

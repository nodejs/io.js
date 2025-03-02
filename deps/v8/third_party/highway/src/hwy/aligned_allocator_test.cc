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

#include "hwy/aligned_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>  // malloc

#include <array>
#include <random>
#include <set>
#include <vector>

#include "hwy/base.h"
#include "hwy/per_target.h"
#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util-inl.h"  // HWY_ASSERT_EQ

namespace {

// Sample object that keeps track on an external counter of how many times was
// the explicit constructor and destructor called.
template <size_t N>
class SampleObject {
 public:
  SampleObject() { data_[0] = 'a'; }
  explicit SampleObject(int* counter) : counter_(counter) {
    if (counter) (*counter)++;
    data_[0] = 'b';
  }

  ~SampleObject() {
    if (counter_) (*counter_)--;
  }

  static_assert(N > sizeof(int*), "SampleObject size too small.");
  int* counter_ = nullptr;
  char data_[N - sizeof(int*)];
};

class FakeAllocator {
 public:
  // static AllocPtr and FreePtr member to be used with the aligned
  // allocator. These functions calls the private non-static members.
  static void* StaticAlloc(void* opaque, size_t bytes) {
    return reinterpret_cast<FakeAllocator*>(opaque)->Alloc(bytes);
  }
  static void StaticFree(void* opaque, void* memory) {
    return reinterpret_cast<FakeAllocator*>(opaque)->Free(memory);
  }

  // Returns the number of pending allocations to be freed.
  size_t PendingAllocs() { return allocs_.size(); }

 private:
  void* Alloc(size_t bytes) {
    void* ret = malloc(bytes);
    allocs_.insert(ret);
    return ret;
  }
  void Free(void* memory) {
    if (!memory) return;
    HWY_ASSERT(allocs_.end() != allocs_.find(memory));
    allocs_.erase(memory);
    free(memory);
  }

  std::set<void*> allocs_;
};

}  // namespace

namespace hwy {
namespace {

#if !HWY_TEST_STANDALONE
class AlignedAllocatorTest : public testing::Test {};
#endif

TEST(AlignedAllocatorTest, TestFreeNullptr) {
  // Calling free with a nullptr is always ok.
  FreeAlignedBytes(/*aligned_pointer=*/nullptr, /*free_ptr=*/nullptr,
                   /*opaque_ptr=*/nullptr);
}

TEST(AlignedAllocatorTest, TestLog2) {
  HWY_ASSERT_EQ(0u, detail::ShiftCount(1));
  HWY_ASSERT_EQ(1u, detail::ShiftCount(2));
  HWY_ASSERT_EQ(3u, detail::ShiftCount(8));
}

// Allocator returns null when it detects overflow of items * sizeof(T).
TEST(AlignedAllocatorTest, TestOverflow) {
  constexpr size_t max = ~size_t(0);
  constexpr size_t msb = (max >> 1) + 1;
  using Size5 = std::array<uint8_t, 5>;
  using Size10 = std::array<uint8_t, 10>;
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<uint32_t>(max / 2, nullptr, nullptr));
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<uint32_t>(max / 3, nullptr, nullptr));
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<Size5>(max / 4, nullptr, nullptr));
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<uint16_t>(msb, nullptr, nullptr));
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<double>(msb + 1, nullptr, nullptr));
  HWY_ASSERT(nullptr ==
             detail::AllocateAlignedItems<Size10>(msb / 4, nullptr, nullptr));
}

TEST(AlignedAllocatorTest, TestAllocDefaultPointers) {
  const size_t kSize = 7777;
  void* ptr = AllocateAlignedBytes(kSize, /*alloc_ptr=*/nullptr,
                                   /*opaque_ptr=*/nullptr);
  HWY_ASSERT(ptr != nullptr);
  // Make sure the pointer is actually aligned.
  HWY_ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % HWY_ALIGNMENT);
  char* p = static_cast<char*>(ptr);
  size_t ret = 0;
  for (size_t i = 0; i < kSize; i++) {
    // Performs a computation using p[] to prevent it being optimized away.
    p[i] = static_cast<char>(i & 0x7F);
    if (i) ret += static_cast<size_t>(p[i] * p[i - 1]);
  }
  HWY_ASSERT(ret != size_t{0});
  FreeAlignedBytes(ptr, /*free_ptr=*/nullptr, /*opaque_ptr=*/nullptr);
}

TEST(AlignedAllocatorTest, TestEmptyAlignedUniquePtr) {
  AlignedUniquePtr<SampleObject<32>> ptr(nullptr, AlignedDeleter());
  AlignedUniquePtr<SampleObject<32>[]> arr(nullptr, AlignedDeleter());
}

TEST(AlignedAllocatorTest, TestEmptyAlignedFreeUniquePtr) {
  AlignedFreeUniquePtr<SampleObject<32>> ptr(nullptr, AlignedFreer());
  AlignedFreeUniquePtr<SampleObject<32>[]> arr(nullptr, AlignedFreer());
}

TEST(AlignedAllocatorTest, TestCustomAlloc) {
  FakeAllocator fake_alloc;

  const size_t kSize = 7777;
  void* ptr =
      AllocateAlignedBytes(kSize, &FakeAllocator::StaticAlloc, &fake_alloc);
  HWY_ASSERT(ptr != nullptr);
  // We should have only requested one alloc from the allocator.
  HWY_ASSERT_EQ(1U, fake_alloc.PendingAllocs());
  // Make sure the pointer is actually aligned.
  HWY_ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr) % HWY_ALIGNMENT);
  FreeAlignedBytes(ptr, &FakeAllocator::StaticFree, &fake_alloc);
  HWY_ASSERT_EQ(0U, fake_alloc.PendingAllocs());
}

TEST(AlignedAllocatorTest, TestMakeUniqueAlignedDefaultConstructor) {
  {
    auto ptr = MakeUniqueAligned<SampleObject<24>>();
    // Default constructor sets the data_[0] to 'a'.
    HWY_ASSERT_EQ('a', ptr->data_[0]);
    HWY_ASSERT(nullptr == ptr->counter_);
  }
}

TEST(AlignedAllocatorTest, TestMakeUniqueAligned) {
  int counter = 0;
  {
    // Creates the object, initializes it with the explicit constructor and
    // returns an unique_ptr to it.
    auto ptr = MakeUniqueAligned<SampleObject<24>>(&counter);
    HWY_ASSERT_EQ(1, counter);
    // Custom constructor sets the data_[0] to 'b'.
    HWY_ASSERT_EQ('b', ptr->data_[0]);
  }
  HWY_ASSERT_EQ(0, counter);
}

TEST(AlignedAllocatorTest, TestMakeUniqueAlignedArray) {
  int counter = 0;
  {
    // Creates the array of objects and initializes them with the explicit
    // constructor.
    auto arr = MakeUniqueAlignedArray<SampleObject<24>>(7, &counter);
    HWY_ASSERT_EQ(7, counter);
    for (size_t i = 0; i < 7; i++) {
      // Custom constructor sets the data_[0] to 'b'.
      HWY_ASSERT_EQ('b', arr[i].data_[0]);
    }
  }
  HWY_ASSERT_EQ(0, counter);
}

TEST(AlignedAllocatorTest, TestAllocSingleInt) {
  auto ptr = AllocateAligned<uint32_t>(1);
  HWY_ASSERT(ptr.get() != nullptr);
  HWY_ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr.get()) % HWY_ALIGNMENT);
  // Force delete of the unique_ptr now to check that it doesn't crash.
  ptr.reset(nullptr);
  HWY_ASSERT(nullptr == ptr.get());
}

TEST(AlignedAllocatorTest, TestAllocMultipleInt) {
  const size_t kSize = 7777;
  auto ptr = AllocateAligned<uint32_t>(kSize);
  HWY_ASSERT(ptr.get() != nullptr);
  HWY_ASSERT_EQ(0U, reinterpret_cast<uintptr_t>(ptr.get()) % HWY_ALIGNMENT);
  // ptr[i] is actually (*ptr.get())[i] which will use the operator[] of the
  // underlying type chosen by AllocateAligned() for the std::unique_ptr.
  HWY_ASSERT(&(ptr[0]) + 1 == &(ptr[1]));

  size_t ret = 0;
  for (size_t i = 0; i < kSize; i++) {
    // Performs a computation using ptr[] to prevent it being optimized away.
    ptr[i] = static_cast<uint32_t>(i);
    if (i) ret += static_cast<size_t>(ptr[i]) * ptr[i - 1];
  }
  HWY_ASSERT(ret != size_t{0});
}

TEST(AlignedAllocatorTest, TestAllocateAlignedObjectWithoutDestructor) {
  int counter = 0;
  {
    // This doesn't call the constructor.
    auto obj = AllocateAligned<SampleObject<24>>(1);
    HWY_ASSERT(obj);
    obj[0].counter_ = &counter;
  }
  // Destroying the unique_ptr shouldn't have called the destructor of the
  // SampleObject<24>.
  HWY_ASSERT_EQ(0, counter);
}

TEST(AlignedAllocatorTest, TestMakeUniqueAlignedArrayWithCustomAlloc) {
  FakeAllocator fake_alloc;
  int counter = 0;
  {
    // Creates the array of objects and initializes them with the explicit
    // constructor.
    auto arr = MakeUniqueAlignedArrayWithAlloc<SampleObject<24>>(
        7, FakeAllocator::StaticAlloc, FakeAllocator::StaticFree, &fake_alloc,
        &counter);
    HWY_ASSERT(arr.get() != nullptr);
    // An array should still only call a single allocation.
    HWY_ASSERT_EQ(1u, fake_alloc.PendingAllocs());
    HWY_ASSERT_EQ(7, counter);
    for (size_t i = 0; i < 7; i++) {
      // Custom constructor sets the data_[0] to 'b'.
      HWY_ASSERT_EQ('b', arr[i].data_[0]);
    }
  }
  HWY_ASSERT_EQ(0, counter);
  HWY_ASSERT_EQ(0u, fake_alloc.PendingAllocs());
}

TEST(AlignedAllocatorTest, TestDefaultInit) {
  // The test is whether this compiles. Default-init is useful for output params
  // and per-thread storage.
  std::vector<AlignedUniquePtr<int[]>> ptrs;
  std::vector<AlignedFreeUniquePtr<double[]>> free_ptrs;
  ptrs.resize(128);
  free_ptrs.resize(128);
  // The following is to prevent elision of the pointers.
  std::mt19937 rng(129);  // Emscripten lacks random_device.
  std::uniform_int_distribution<size_t> dist(0, 127);
  ptrs[dist(rng)] = MakeUniqueAlignedArray<int>(123);
  free_ptrs[dist(rng)] = AllocateAligned<double>(456);
  // "Use" pointer without resorting to printf. 0 == 0. Can't shift by 64.
  const auto addr1 = reinterpret_cast<uintptr_t>(ptrs[dist(rng)].get());
  const auto addr2 = reinterpret_cast<uintptr_t>(free_ptrs[dist(rng)].get());
  constexpr size_t kBits = sizeof(uintptr_t) * 8;
  HWY_ASSERT_EQ((addr1 >> (kBits - 1)) >> (kBits - 1),
                (addr2 >> (kBits - 1)) >> (kBits - 1));
}

using std::array;
using std::vector;

template <typename T>
void CheckEqual(const T& t1, const T& t2) {
  HWY_ASSERT_EQ(t1.size(), t2.size());
  for (size_t i = 0; i < t1.size(); i++) {
    HWY_ASSERT_EQ(t1[i], t2[i]);
  }
}

template <typename T>
void CheckEqual(const AlignedNDArray<T, 1>& a, const vector<T>& v) {
  const array<size_t, 1> want_shape({v.size()});
  const array<size_t, 1> got_shape = a.shape();
  CheckEqual(got_shape, want_shape);

  Span<const T> a_span = a[{}];
  HWY_ASSERT_EQ(a_span.size(), v.size());
  for (size_t i = 0; i < a_span.size(); i++) {
    HWY_ASSERT_EQ(a_span[i], v[i]);
    HWY_ASSERT_EQ(*(a_span.data() + i), v[i]);
  }
}

template <typename T>
void CheckEqual(const AlignedNDArray<T, 2>& a, const vector<vector<T>>& v) {
  const array<size_t, 2> want_shape({v.size(), v[1].size()});
  for (const vector<T>& row : v) {
    HWY_ASSERT_EQ(row.size(), want_shape[1]);
  }
  const std::array<size_t, 2> got_shape = a.shape();
  CheckEqual(got_shape, want_shape);

  HWY_ASSERT_EQ(a.size(), want_shape[0] * want_shape[1]);

  for (size_t row_index = 0; row_index < v.size(); ++row_index) {
    vector<T> want_row = v[row_index];
    Span<const T> got_row = a[{row_index}];
    HWY_ASSERT_EQ(got_row.size(), want_row.size());
    for (size_t column_index = 0; column_index < got_row.size();
         column_index++) {
      HWY_ASSERT_EQ(a[{row_index}][column_index], want_row[column_index]);
      HWY_ASSERT_EQ(got_row[column_index], want_row[column_index]);
      HWY_ASSERT_EQ(*(a[{row_index}].data() + column_index),
                    want_row[column_index]);
    }
  }
}

TEST(AlignedAllocatorTest, TestAlignedNDArray) {
  AlignedNDArray<float, 1> a1({4});
  CheckEqual(a1, {0, 0, 0, 0});
  a1[{}][2] = 3.4f;
  CheckEqual(a1, {0, 0, 3.4f, 0});

  AlignedNDArray<float, 2> a2({2, 3});
  CheckEqual(a2, {{0, 0, 0}, {0, 0, 0}});
  a2[{1}][1] = 5.1f;
  CheckEqual(a2, {{0, 0, 0}, {0, 5.1f, 0}});
  float f0[] = {1.0f, 2.0f, 3.0f};
  float f1[] = {4.0f, 5.0f, 6.0f};
  hwy::CopyBytes(f0, a2[{0}].data(), 3 * sizeof(float));
  hwy::CopyBytes(f1, a2[{1}].data(), 3 * sizeof(float));
  CheckEqual(a2, {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}});
}

// Tests that each innermost row in an AlignedNDArray is aligned to the max
// bytes available for SIMD operations on this architecture.
TEST(AlignedAllocatorTest, TestAlignedNDArrayAlignment) {
  AlignedNDArray<float, 4> a({3, 3, 3, 3});
  for (size_t d0 = 0; d0 < a.shape()[0]; d0++) {
    for (size_t d1 = 0; d1 < a.shape()[1]; d1++) {
      for (size_t d2 = 0; d2 < a.shape()[2]; d2++) {
        // Check that the address this innermost array starts at is an even
        // number of VectorBytes(), which is the max bytes available for SIMD
        // operations.
        HWY_ASSERT_EQ(
            reinterpret_cast<uintptr_t>(a[{d0, d1, d2}].data()) % VectorBytes(),
            0);
      }
    }
  }
}

TEST(AlignedAllocatorTest, TestSpanCopyAssignment) {
  AlignedNDArray<float, 2> a({2, 2});
  CheckEqual(a, {{0.0f, 0.0f}, {0.0f, 0.0f}});
  a[{0}] = {1.0f, 2.0f};
  a[{1}] = {3.0f, 4.0f};
  CheckEqual(a, {{1.0f, 2.0f}, {3.0f, 4.0f}});
}

TEST(AlignedAllocatorTest, TestAlignedNDArrayTruncate) {
  AlignedNDArray<size_t, 4> a({8, 8, 8, 8});
  const size_t last_axis_memory_shape = a.memory_shape()[3];
  const auto compute_value = [&](const std::array<size_t, 4>& index) {
    return index[0] * 8 * 8 * 8 + index[1] * 8 * 8 + index[2] * 8 * 8 +
           index[3];
  };
  for (size_t axis0 = 0; axis0 < a.shape()[0]; ++axis0) {
    for (size_t axis1 = 0; axis1 < a.shape()[1]; ++axis1) {
      for (size_t axis2 = 0; axis2 < a.shape()[2]; ++axis2) {
        for (size_t axis3 = 0; axis3 < a.shape()[3]; ++axis3) {
          a[{axis0, axis1, axis2}][axis3] =
              compute_value({axis0, axis1, axis2, axis3});
        }
      }
    }
  }
  const auto verify_values = [&](const AlignedNDArray<size_t, 4>& array) {
    for (size_t axis0 = 0; axis0 < array.shape()[0]; ++axis0) {
      for (size_t axis1 = 0; axis1 < array.shape()[1]; ++axis1) {
        for (size_t axis2 = 0; axis2 < array.shape()[2]; ++axis2) {
          for (size_t axis3 = 0; axis3 < array.shape()[3]; ++axis3) {
            HWY_ASSERT_EQ((array[{axis0, axis1, axis2}][axis3]),
                          (compute_value({axis0, axis1, axis2, axis3})));
          }
        }
      }
    }
  };
  a.truncate({7, 7, 7, 7});
  HWY_ASSERT_EQ(a.shape()[0], 7);
  HWY_ASSERT_EQ(a.shape()[1], 7);
  HWY_ASSERT_EQ(a.shape()[2], 7);
  HWY_ASSERT_EQ(a.shape()[3], 7);
  HWY_ASSERT_EQ(a.memory_shape()[0], 8);
  HWY_ASSERT_EQ(a.memory_shape()[1], 8);
  HWY_ASSERT_EQ(a.memory_shape()[2], 8);
  HWY_ASSERT_EQ(a.memory_shape()[3], last_axis_memory_shape);
  verify_values(a);
  a.truncate({6, 5, 4, 3});
  HWY_ASSERT_EQ(a.shape()[0], 6);
  HWY_ASSERT_EQ(a.shape()[1], 5);
  HWY_ASSERT_EQ(a.shape()[2], 4);
  HWY_ASSERT_EQ(a.shape()[3], 3);
  HWY_ASSERT_EQ(a.memory_shape()[0], 8);
  HWY_ASSERT_EQ(a.memory_shape()[1], 8);
  HWY_ASSERT_EQ(a.memory_shape()[2], 8);
  HWY_ASSERT_EQ(a.memory_shape()[3], last_axis_memory_shape);
  verify_values(a);
}

TEST(AlignedAllocatorTest, TestAlignedVector) {
  std::vector<int> vec{0, 1, 2, 3, 4};
  HWY_ASSERT_EQ(5, vec.size());
  HWY_ASSERT_EQ(0, vec[0]);
  HWY_ASSERT_EQ(2, vec.at(2));
  HWY_ASSERT_EQ(0, vec.front());
  HWY_ASSERT_EQ(4, vec.back());

  vec.pop_back();
  HWY_ASSERT_EQ(3, vec.back());
  HWY_ASSERT_EQ(4, vec.size());

  vec.push_back(4);
  vec.push_back(5);
  HWY_ASSERT_EQ(5, vec.back());
  HWY_ASSERT_EQ(6, vec.size());

  const size_t initialCapacity = vec.capacity();

  // Add elements to exceed initial capacity
  for (auto i = vec.size(); i < initialCapacity + 10; ++i) {
    vec.push_back(static_cast<int>(i));
  }

  // Check if the capacity increased and elements are intact
  HWY_ASSERT(vec.capacity() > initialCapacity);
  for (size_t i = 0; i < vec.size(); ++i) {
    HWY_ASSERT_EQ(i, vec[i]);
  }

  vec.clear();
  HWY_ASSERT(vec.empty());
}

}  // namespace
}  // namespace hwy

HWY_TEST_MAIN();

// Copyright 2019 Google LLC
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

#include "hwy/base.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/cast_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// Cast and ensure bytes are the same. Called directly from TestAllBitCast or
// via TestBitCastFrom.
template <typename ToT>
struct TestBitCast {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Repartition<ToT, D> dto;
    const size_t N = Lanes(d);
    const size_t Nto = Lanes(dto);
    if (N == 0 || Nto == 0) return;
    HWY_ASSERT_EQ(N * sizeof(T), Nto * sizeof(ToT));
    const auto vf = Iota(d, 1);
    const auto vt = BitCast(dto, vf);
    // Must return the same bits
    auto from_lanes = AllocateAligned<T>(Lanes(d));
    auto to_lanes = AllocateAligned<ToT>(Lanes(dto));
    HWY_ASSERT(from_lanes && to_lanes);
    Store(vf, d, from_lanes.get());
    Store(vt, dto, to_lanes.get());
    HWY_ASSERT(
        BytesEqual(from_lanes.get(), to_lanes.get(), Lanes(d) * sizeof(T)));
  }
};

// From D to all types.
struct TestBitCastFrom {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    TestBitCast<uint8_t>()(t, d);
    TestBitCast<uint16_t>()(t, d);
    TestBitCast<uint32_t>()(t, d);
#if HWY_HAVE_INTEGER64
    TestBitCast<uint64_t>()(t, d);
#endif
    TestBitCast<int8_t>()(t, d);
    TestBitCast<int16_t>()(t, d);
    TestBitCast<int32_t>()(t, d);
#if HWY_HAVE_INTEGER64
    TestBitCast<int64_t>()(t, d);
#endif
    TestBitCast<float>()(t, d);
#if HWY_HAVE_FLOAT64
    TestBitCast<double>()(t, d);
#endif
  }
};

HWY_NOINLINE void TestAllBitCast() {
  // For HWY_SCALAR and partial vectors, we can only cast to same-sized types:
  // the former can't partition its single lane, and the latter can be smaller
  // than a destination type.
  const ForPartialVectors<TestBitCast<uint8_t>> to_u8;
  to_u8(uint8_t());
  to_u8(int8_t());

  const ForPartialVectors<TestBitCast<int8_t>> to_i8;
  to_i8(uint8_t());
  to_i8(int8_t());

  const ForPartialVectors<TestBitCast<uint16_t>> to_u16;
  to_u16(uint16_t());
  to_u16(int16_t());

  const ForPartialVectors<TestBitCast<int16_t>> to_i16;
  to_i16(uint16_t());
  to_i16(int16_t());

  const ForPartialVectors<TestBitCast<uint32_t>> to_u32;
  to_u32(uint32_t());
  to_u32(int32_t());
  to_u32(float());

  const ForPartialVectors<TestBitCast<int32_t>> to_i32;
  to_i32(uint32_t());
  to_i32(int32_t());
  to_i32(float());

#if HWY_HAVE_INTEGER64
  const ForPartialVectors<TestBitCast<uint64_t>> to_u64;
  to_u64(uint64_t());
  to_u64(int64_t());
#if HWY_HAVE_FLOAT64
  to_u64(double());
#endif

  const ForPartialVectors<TestBitCast<int64_t>> to_i64;
  to_i64(uint64_t());
  to_i64(int64_t());
#if HWY_HAVE_FLOAT64
  to_i64(double());
#endif
#endif  // HWY_HAVE_INTEGER64

  const ForPartialVectors<TestBitCast<float>> to_float;
  to_float(uint32_t());
  to_float(int32_t());
  to_float(float());

#if HWY_HAVE_FLOAT64
  const ForPartialVectors<TestBitCast<double>> to_double;
  to_double(double());
#if HWY_HAVE_INTEGER64
  to_double(uint64_t());
  to_double(int64_t());
#endif  // HWY_HAVE_INTEGER64
#endif  // HWY_HAVE_FLOAT64

#if HWY_TARGET != HWY_SCALAR
  // For non-scalar vectors, we can cast all types to all.
  ForAllTypes(ForGEVectors<64, TestBitCastFrom>());
#endif
}

template <class TTo>
struct TestResizeBitCastToOneLaneVect {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    if (N == 0) {
      return;
    }

    auto from_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(from_lanes);
    auto v = Iota(d, 1);
    Store(v, d, from_lanes.get());

    const size_t num_of_bytes_to_copy = HWY_MIN(N * sizeof(T), sizeof(TTo));

    int8_t active_bits_mask_i8_arr[sizeof(TTo)] = {};
    for (size_t i = 0; i < num_of_bytes_to_copy; i++) {
      active_bits_mask_i8_arr[i] = int8_t{-1};
    }

    TTo active_bits_int_mask;
    CopyBytes<sizeof(TTo)>(active_bits_mask_i8_arr, &active_bits_int_mask);

    const FixedTag<TTo, 1> d_to;
    TTo expected_bits = 0;
    CopyBytes(from_lanes.get(), &expected_bits, num_of_bytes_to_copy);

    const auto expected = Set(d_to, expected_bits);
    const auto v_active_bits_mask = Set(d_to, active_bits_int_mask);

    const auto actual_1 = And(v_active_bits_mask, ResizeBitCast(d_to, v));
    const auto actual_2 = ZeroExtendResizeBitCast(d_to, d, v);

    HWY_ASSERT_VEC_EQ(d_to, expected, actual_1);
    HWY_ASSERT_VEC_EQ(d_to, expected, actual_2);
  }
};

HWY_NOINLINE void TestAllResizeBitCastToOneLaneVect() {
  ForAllTypes(ForPartialVectors<TestResizeBitCastToOneLaneVect<uint32_t>>());
#if HWY_HAVE_INTEGER64
  ForAllTypes(ForPartialVectors<TestResizeBitCastToOneLaneVect<uint64_t>>());
#endif
}

// Cast and ensure bytes are the same. Called directly from
// TestAllSameSizeResizeBitCast or via TestSameSizeResizeBitCastFrom.
template <typename ToT>
struct TestSameSizeResizeBitCast {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Repartition<ToT, D> dto;
    const auto v = Iota(d, 1);
    const auto expected = BitCast(dto, v);

    const VFromD<decltype(dto)> actual_1 = ResizeBitCast(dto, v);
    const VFromD<decltype(dto)> actual_2 = ZeroExtendResizeBitCast(dto, d, v);

    HWY_ASSERT_VEC_EQ(dto, expected, actual_1);
    HWY_ASSERT_VEC_EQ(dto, expected, actual_2);
  }
};

// From D to all types.
struct TestSameSizeResizeBitCastFrom {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T t, D d) {
    TestSameSizeResizeBitCast<uint8_t>()(t, d);
    TestSameSizeResizeBitCast<uint16_t>()(t, d);
    TestSameSizeResizeBitCast<uint32_t>()(t, d);
#if HWY_HAVE_INTEGER64
    TestSameSizeResizeBitCast<uint64_t>()(t, d);
#endif
    TestSameSizeResizeBitCast<int8_t>()(t, d);
    TestSameSizeResizeBitCast<int16_t>()(t, d);
    TestSameSizeResizeBitCast<int32_t>()(t, d);
#if HWY_HAVE_INTEGER64
    TestSameSizeResizeBitCast<int64_t>()(t, d);
#endif
    TestSameSizeResizeBitCast<float>()(t, d);
#if HWY_HAVE_FLOAT64
    TestSameSizeResizeBitCast<double>()(t, d);
#endif
  }
};

HWY_NOINLINE void TestAllSameSizeResizeBitCast() {
  // For HWY_SCALAR and partial vectors, we can only cast to same-sized types:
  // the former can't partition its single lane, and the latter can be smaller
  // than a destination type.
  const ForPartialVectors<TestSameSizeResizeBitCast<uint8_t>> to_u8;
  to_u8(uint8_t());
  to_u8(int8_t());

  const ForPartialVectors<TestSameSizeResizeBitCast<int8_t>> to_i8;
  to_i8(uint8_t());
  to_i8(int8_t());

  const ForPartialVectors<TestSameSizeResizeBitCast<uint16_t>> to_u16;
  to_u16(uint16_t());
  to_u16(int16_t());

  const ForPartialVectors<TestSameSizeResizeBitCast<int16_t>> to_i16;
  to_i16(uint16_t());
  to_i16(int16_t());

  const ForPartialVectors<TestSameSizeResizeBitCast<uint32_t>> to_u32;
  to_u32(uint32_t());
  to_u32(int32_t());
  to_u32(float());

  const ForPartialVectors<TestSameSizeResizeBitCast<int32_t>> to_i32;
  to_i32(uint32_t());
  to_i32(int32_t());
  to_i32(float());

#if HWY_HAVE_INTEGER64
  const ForPartialVectors<TestSameSizeResizeBitCast<uint64_t>> to_u64;
  to_u64(uint64_t());
  to_u64(int64_t());
#if HWY_HAVE_FLOAT64
  to_u64(double());
#endif

  const ForPartialVectors<TestSameSizeResizeBitCast<int64_t>> to_i64;
  to_i64(uint64_t());
  to_i64(int64_t());
#if HWY_HAVE_FLOAT64
  to_i64(double());
#endif
#endif  // HWY_HAVE_INTEGER64

  const ForPartialVectors<TestSameSizeResizeBitCast<float>> to_float;
  to_float(uint32_t());
  to_float(int32_t());
  to_float(float());

#if HWY_HAVE_FLOAT64
  const ForPartialVectors<TestSameSizeResizeBitCast<double>> to_double;
  to_double(double());
#if HWY_HAVE_INTEGER64
  to_double(uint64_t());
  to_double(int64_t());
#endif  // HWY_HAVE_INTEGER64
#endif  // HWY_HAVE_FLOAT64

#if HWY_TARGET != HWY_SCALAR
  // For non-scalar vectors, we can cast all types to all.
  ForAllTypes(ForGEVectors<64, TestSameSizeResizeBitCastFrom>());
#endif
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCastTest);
HWY_EXPORT_AND_TEST_P(HwyCastTest, TestAllBitCast);
HWY_EXPORT_AND_TEST_P(HwyCastTest, TestAllResizeBitCastToOneLaneVect);
HWY_EXPORT_AND_TEST_P(HwyCastTest, TestAllSameSizeResizeBitCast);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE

// Copyright 2023 Google LLC
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

#include <stdio.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/tuple_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestCreateAndSet {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_HAVE_TUPLE
    const Vec<D> v0 = Zero(d);
    const Vec<D> vi = Iota(d, 1);
    const Vec<D> v2 = Set(d, ConvertScalarTo<T>(2));
    const Vec<D> v3 = Set(d, ConvertScalarTo<T>(3));

    Vec2<D> t2 = Create2(d, v0, vi);
    HWY_ASSERT_VEC_EQ(d, v0, Get2<0>(t2));
    HWY_ASSERT_VEC_EQ(d, vi, Get2<1>(t2));

    t2 = Set2<0>(t2, vi);
    t2 = Set2<1>(t2, v0);
    HWY_ASSERT_VEC_EQ(d, vi, Get2<0>(t2));
    HWY_ASSERT_VEC_EQ(d, v0, Get2<1>(t2));

    Vec3<D> t3 = Create3(d, v0, vi, v2);
    HWY_ASSERT_VEC_EQ(d, v0, Get3<0>(t3));
    HWY_ASSERT_VEC_EQ(d, vi, Get3<1>(t3));
    HWY_ASSERT_VEC_EQ(d, v2, Get3<2>(t3));

    t3 = Set3<0>(t3, v2);
    t3 = Set3<1>(t3, vi);
    t3 = Set3<2>(t3, v0);
    HWY_ASSERT_VEC_EQ(d, v2, Get3<0>(t3));
    HWY_ASSERT_VEC_EQ(d, vi, Get3<1>(t3));
    HWY_ASSERT_VEC_EQ(d, v0, Get3<2>(t3));

    Vec4<D> t4 = Create4(d, v0, vi, v2, v3);
    HWY_ASSERT_VEC_EQ(d, v0, Get4<0>(t4));
    HWY_ASSERT_VEC_EQ(d, vi, Get4<1>(t4));
    HWY_ASSERT_VEC_EQ(d, v2, Get4<2>(t4));
    HWY_ASSERT_VEC_EQ(d, v3, Get4<3>(t4));

    t4 = Set4<0>(t4, v3);
    t4 = Set4<1>(t4, v2);
    t4 = Set4<2>(t4, vi);
    t4 = Set4<3>(t4, v0);
    HWY_ASSERT_VEC_EQ(d, v3, Get4<0>(t4));
    HWY_ASSERT_VEC_EQ(d, v2, Get4<1>(t4));
    HWY_ASSERT_VEC_EQ(d, vi, Get4<2>(t4));
    HWY_ASSERT_VEC_EQ(d, v0, Get4<3>(t4));
#else
    (void)d;
    fprintf(stderr, "Warning: tuples are disabled for target %s\n",
            hwy::TargetName(HWY_TARGET));
#endif  // HWY_HAVE_TUPLE
  }
};

HWY_NOINLINE void TestAllCreate() {
  // RVV can only do tuples up to LMUL=2.
  ForAllTypes(ForMaxPow2<TestCreateAndSet>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(TupleTest);
HWY_EXPORT_AND_TEST_P(TupleTest, TestAllCreate);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE

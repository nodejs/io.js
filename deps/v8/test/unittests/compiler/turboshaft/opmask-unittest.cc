// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/operations.h"
#define TURBOSHAFT_OPMASK_EXPORT_FIELD_MACRO_FOR_UNITTESTS
#include "src/compiler/turboshaft/opmasks.h"
#include "testing/gtest-support.h"

namespace v8::internal::compiler::turboshaft {

struct MyFakeOp;

// We reuse `Opcode::kConstant` because extending the opcode enum is hard from
// within the test.
template <>
struct operation_to_opcode<MyFakeOp>
    : std::integral_constant<Opcode, Opcode::kConstant> {};

struct MyFakeOp : FixedArityOperationT<0, MyFakeOp> {
  enum class Kind : uint16_t {
    kA = 0x0000,
    kB = 0x0001,
    kC = 0x0100,
    kD = 0x11F8,
    kE = 0xFFFF,
  };
  Kind kind;
  uint16_t value;

  MyFakeOp(Kind kind, uint16_t value) : Base(), kind(kind), value(value) {}
};

using namespace Opmask;
using MyFakeMask = Opmask::MaskBuilder<MyFakeOp, FIELD(MyFakeOp, kind),
                                       FIELD(MyFakeOp, value)>;
using kA0 = MyFakeMask::For<MyFakeOp::Kind::kA, 0>;
using kB0 = MyFakeMask::For<MyFakeOp::Kind::kB, 0>;
using kC0 = MyFakeMask::For<MyFakeOp::Kind::kC, 0>;
using kD0 = MyFakeMask::For<MyFakeOp::Kind::kD, 0>;
using kA1 = MyFakeMask::For<MyFakeOp::Kind::kA, 1>;
using kC1 = MyFakeMask::For<MyFakeOp::Kind::kC, 1>;
using kB0100 = MyFakeMask::For<MyFakeOp::Kind::kB, 0x0100>;
using kD0100 = MyFakeMask::For<MyFakeOp::Kind::kD, 0x0100>;
using kA11F8 = MyFakeMask::For<MyFakeOp::Kind::kA, 0x11F8>;
using kB11F8 = MyFakeMask::For<MyFakeOp::Kind::kB, 0x11F8>;

using MyFakeKindMask = Opmask::MaskBuilder<MyFakeOp, FIELD(MyFakeOp, kind)>;
using kA = MyFakeKindMask::For<MyFakeOp::Kind::kA>;
using kC = MyFakeKindMask::For<MyFakeOp::Kind::kC>;

class OpmaskTest : public ::testing::Test {};

template <typename... CandidateList>
struct MaskList;

template <typename Head, typename... Tail>
struct MaskList<Head, Tail...> {
  template <typename Expected>
  static void Check(const MyFakeOp& op) {
    ASSERT_EQ(op.template Is<Head>(), (std::is_same_v<Expected, Head>));
    MaskList<Tail...>::template Check<Expected>(op);
  }
};

template <>
struct MaskList<> {
  template <typename Expected>
  static void Check(const MyFakeOp&) {}
};

template <typename Expected>
void Check(const MyFakeOp& op) {
  MaskList<kA0, kB0, kC0, kD0, kA1, kC1, kB0100, kD0100, kA11F8,
           kB11F8>::Check<Expected>(op);
}

TEST_F(OpmaskTest, FullMask) {
  MyFakeOp op_A0(MyFakeOp::Kind::kA, 0);
  Check<kA0>(op_A0);

  MyFakeOp op_B0(MyFakeOp::Kind::kB, 0);
  Check<kB0>(op_B0);

  MyFakeOp op_C1(MyFakeOp::Kind::kC, 1);
  Check<kC1>(op_C1);

  MyFakeOp op_B0100(MyFakeOp::Kind::kB, 0x0100);
  Check<kB0100>(op_B0100);

  MyFakeOp op_D0100(MyFakeOp::Kind::kD, 0x0100);
  Check<kD0100>(op_D0100);

  MyFakeOp op_A11F8(MyFakeOp::Kind::kA, 0x11F8);
  Check<kA11F8>(op_A11F8);

  // Ops that should not match any mask.
  MyFakeOp op_other1(MyFakeOp::Kind::kE, 0);
  Check<void>(op_other1);
  MyFakeOp op_other2(MyFakeOp::Kind::kE, 0x11F8);
  Check<void>(op_other2);
  MyFakeOp op_other3(MyFakeOp::Kind::kA, 2);
  Check<void>(op_other3);
  MyFakeOp op_other4(MyFakeOp::Kind::kD, 0xF811);
  Check<void>(op_other4);
  MyFakeOp op_other5(MyFakeOp::Kind::kA, 0x0100);
  Check<void>(op_other5);
}

TEST_F(OpmaskTest, PartialMask) {
  for (uint16_t v : {0, 1, 2, 0x0100, 0x0101, 0x11F8}) {
    MyFakeOp op(MyFakeOp::Kind::kA, v);
    ASSERT_TRUE(op.Is<kA>());
    ASSERT_FALSE(op.Is<kC>());
  }

  for (uint16_t v : {0, 1, 2, 0x0100, 0x0101, 0x11F8}) {
    MyFakeOp op(MyFakeOp::Kind::kC, v);
    ASSERT_FALSE(op.Is<kA>());
    ASSERT_TRUE(op.Is<kC>());
  }
}

}  // namespace v8::internal::compiler::turboshaft

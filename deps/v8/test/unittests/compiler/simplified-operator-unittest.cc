// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-operator.h"

#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-types.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace simplified_operator_unittest {

// -----------------------------------------------------------------------------

// Pure operators.

struct PureOperator {
  const Operator* (SimplifiedOperatorBuilder::*constructor)();
  IrOpcode::Value opcode;
  Operator::Properties properties;
  int value_input_count;
};


std::ostream& operator<<(std::ostream& os, const PureOperator& pop) {
  return os << IrOpcode::Mnemonic(pop.opcode);
}

const PureOperator kPureOperators[] = {
#define PURE(Name, properties, input_count)              \
  {                                                      \
    &SimplifiedOperatorBuilder::Name, IrOpcode::k##Name, \
        Operator::kPure | properties, input_count        \
  }
    PURE(BooleanNot, Operator::kNoProperties, 1),
    PURE(NumberEqual, Operator::kCommutative, 2),
    PURE(NumberLessThan, Operator::kNoProperties, 2),
    PURE(NumberLessThanOrEqual, Operator::kNoProperties, 2),
    PURE(NumberAdd, Operator::kCommutative, 2),
    PURE(NumberSubtract, Operator::kNoProperties, 2),
    PURE(NumberMultiply, Operator::kCommutative, 2),
    PURE(NumberDivide, Operator::kNoProperties, 2),
    PURE(NumberModulus, Operator::kNoProperties, 2),
    PURE(NumberBitwiseOr, Operator::kCommutative, 2),
    PURE(NumberBitwiseXor, Operator::kCommutative, 2),
    PURE(NumberBitwiseAnd, Operator::kCommutative, 2),
    PURE(NumberShiftLeft, Operator::kNoProperties, 2),
    PURE(NumberShiftRight, Operator::kNoProperties, 2),
    PURE(NumberShiftRightLogical, Operator::kNoProperties, 2),
    PURE(NumberToInt32, Operator::kNoProperties, 1),
    PURE(NumberToUint32, Operator::kNoProperties, 1),
    PURE(ChangeTaggedSignedToInt32, Operator::kNoProperties, 1),
    PURE(ChangeTaggedToInt32, Operator::kNoProperties, 1),
    PURE(ChangeTaggedToUint32, Operator::kNoProperties, 1),
    PURE(ChangeTaggedToFloat64, Operator::kNoProperties, 1),
    PURE(ChangeInt32ToTagged, Operator::kNoProperties, 1),
    PURE(ChangeUint32ToTagged, Operator::kNoProperties, 1),
    PURE(ChangeTaggedToBit, Operator::kNoProperties, 1),
    PURE(ChangeBitToTagged, Operator::kNoProperties, 1),
    PURE(TruncateTaggedToWord32, Operator::kNoProperties, 1),
    PURE(TruncateTaggedToFloat64, Operator::kNoProperties, 1),
    PURE(TruncateTaggedToBit, Operator::kNoProperties, 1),
    PURE(ObjectIsNumber, Operator::kNoProperties, 1),
    PURE(ObjectIsReceiver, Operator::kNoProperties, 1),
    PURE(ObjectIsSmi, Operator::kNoProperties, 1),
#undef PURE
};


class SimplifiedPureOperatorTest
    : public TestWithZone,
      public ::testing::WithParamInterface<PureOperator> {};


TEST_P(SimplifiedPureOperatorTest, InstancesAreGloballyShared) {
  const PureOperator& pop = GetParam();
  SimplifiedOperatorBuilder simplified1(zone());
  SimplifiedOperatorBuilder simplified2(zone());
  EXPECT_EQ((simplified1.*pop.constructor)(), (simplified2.*pop.constructor)());
}


TEST_P(SimplifiedPureOperatorTest, NumberOfInputsAndOutputs) {
  SimplifiedOperatorBuilder simplified(zone());
  const PureOperator& pop = GetParam();
  const Operator* op = (simplified.*pop.constructor)();

  EXPECT_EQ(pop.value_input_count, op->ValueInputCount());
  EXPECT_EQ(0, op->EffectInputCount());
  EXPECT_EQ(0, op->ControlInputCount());
  EXPECT_EQ(pop.value_input_count, OperatorProperties::GetTotalInputCount(op));

  EXPECT_EQ(1, op->ValueOutputCount());
  EXPECT_EQ(0, op->EffectOutputCount());
  EXPECT_EQ(0, op->ControlOutputCount());
}


TEST_P(SimplifiedPureOperatorTest, OpcodeIsCorrect) {
  SimplifiedOperatorBuilder simplified(zone());
  const PureOperator& pop = GetParam();
  const Operator* op = (simplified.*pop.constructor)();
  EXPECT_EQ(pop.opcode, op->opcode());
}


TEST_P(SimplifiedPureOperatorTest, Properties) {
  SimplifiedOperatorBuilder simplified(zone());
  const PureOperator& pop = GetParam();
  const Operator* op = (simplified.*pop.constructor)();
  EXPECT_EQ(pop.properties, op->properties() & pop.properties);
}

INSTANTIATE_TEST_SUITE_P(SimplifiedOperatorTest, SimplifiedPureOperatorTest,
                         ::testing::ValuesIn(kPureOperators));

// -----------------------------------------------------------------------------

// Element access operators.

const ElementAccess kElementAccesses[] = {
    {kTaggedBase, OFFSET_OF_DATA_START(FixedArray), Type::Any(),
     MachineType::AnyTagged(), kFullWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Int8(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Int16(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Int32(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Uint8(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Uint16(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Any(), MachineType::Uint32(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Signed32(), MachineType::Int8(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Unsigned32(), MachineType::Uint8(),
     kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Signed32(), MachineType::Int16(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Unsigned32(), MachineType::Uint16(),
     kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Signed32(), MachineType::Int32(), kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Unsigned32(), MachineType::Uint32(),
     kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Number(),
     MachineType(MachineRepresentation::kFloat32, MachineSemantic::kNone),
     kNoWriteBarrier},
    {kUntaggedBase, 0, Type::Number(),
     MachineType(MachineRepresentation::kFloat64, MachineSemantic::kNone),
     kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Signed32(),
     MachineType::Int8(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Unsigned32(),
     MachineType::Uint8(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Signed32(),
     MachineType::Int16(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Unsigned32(),
     MachineType::Uint16(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Signed32(),
     MachineType::Int32(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Unsigned32(),
     MachineType::Uint32(), kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Number(),
     MachineType(MachineRepresentation::kFloat32, MachineSemantic::kNone),
     kNoWriteBarrier},
    {kTaggedBase, OFFSET_OF_DATA_START(ByteArray), Type::Number(),
     MachineType(MachineRepresentation::kFloat32, MachineSemantic::kNone),
     kNoWriteBarrier}};

class SimplifiedElementAccessOperatorTest
    : public TestWithZone,
      public ::testing::WithParamInterface<ElementAccess> {};


TEST_P(SimplifiedElementAccessOperatorTest, LoadElement) {
  SimplifiedOperatorBuilder simplified(zone());
  const ElementAccess& access = GetParam();
  const Operator* op = simplified.LoadElement(access);

  EXPECT_EQ(IrOpcode::kLoadElement, op->opcode());
  EXPECT_EQ(Operator::kNoDeopt | Operator::kNoThrow | Operator::kNoWrite,
            op->properties());
  EXPECT_EQ(access, ElementAccessOf(op));

  EXPECT_EQ(2, op->ValueInputCount());
  EXPECT_EQ(1, op->EffectInputCount());
  EXPECT_EQ(1, op->ControlInputCount());
  EXPECT_EQ(4, OperatorProperties::GetTotalInputCount(op));

  EXPECT_EQ(1, op->ValueOutputCount());
  EXPECT_EQ(1, op->EffectOutputCount());
  EXPECT_EQ(0, op->ControlOutputCount());
}


TEST_P(SimplifiedElementAccessOperatorTest, StoreElement) {
  SimplifiedOperatorBuilder simplified(zone());
  const ElementAccess& access = GetParam();
  const Operator* op = simplified.StoreElement(access);

  EXPECT_EQ(IrOpcode::kStoreElement, op->opcode());
  EXPECT_EQ(Operator::kNoDeopt | Operator::kNoRead | Operator::kNoThrow,
            op->properties());
  EXPECT_EQ(access, ElementAccessOf(op));

  EXPECT_EQ(3, op->ValueInputCount());
  EXPECT_EQ(1, op->EffectInputCount());
  EXPECT_EQ(1, op->ControlInputCount());
  EXPECT_EQ(5, OperatorProperties::GetTotalInputCount(op));

  EXPECT_EQ(0, op->ValueOutputCount());
  EXPECT_EQ(1, op->EffectOutputCount());
  EXPECT_EQ(0, op->ControlOutputCount());
}

INSTANTIATE_TEST_SUITE_P(SimplifiedOperatorTest,
                         SimplifiedElementAccessOperatorTest,
                         ::testing::ValuesIn(kElementAccesses));

}  // namespace simplified_operator_unittest
}  // namespace compiler
}  // namespace internal
}  // namespace v8

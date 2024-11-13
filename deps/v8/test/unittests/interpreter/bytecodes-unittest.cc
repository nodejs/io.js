// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecodes.h"

#include <vector>

#include "src/init/v8.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-register.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

TEST(OperandConversion, Registers) {
  int register_count = 128;
  int step = register_count / 7;
  for (int i = 0; i < register_count; i += step) {
    if (i <= kMaxInt8) {
      uint32_t operand0 = Register(i).ToOperand();
      Register reg0 = Register::FromOperand(operand0);
      CHECK_EQ(i, reg0.index());
    }

    uint32_t operand1 = Register(i).ToOperand();
    Register reg1 = Register::FromOperand(operand1);
    CHECK_EQ(i, reg1.index());

    uint32_t operand2 = Register(i).ToOperand();
    Register reg2 = Register::FromOperand(operand2);
    CHECK_EQ(i, reg2.index());
  }
}

TEST(OperandConversion, Parameters) {
  int parameter_counts[] = {7, 13, 99};

  size_t count = sizeof(parameter_counts) / sizeof(parameter_counts[0]);
  for (size_t p = 0; p < count; p++) {
    int parameter_count = parameter_counts[p];
    for (int i = 0; i < parameter_count; i++) {
      Register r = Register::FromParameterIndex(i);
      uint32_t operand_value = r.ToOperand();
      Register s = Register::FromOperand(operand_value);
      CHECK_EQ(i, s.ToParameterIndex());
    }
  }
}

TEST(OperandConversion, RegistersParametersNoOverlap) {
  int register_count = 128;
  int parameter_count = 100;
  int32_t register_space_size = base::bits::RoundUpToPowerOfTwo32(
      static_cast<uint32_t>(register_count + parameter_count));
  uint32_t range = static_cast<uint32_t>(register_space_size);
  std::vector<uint8_t> operand_count(range);

  for (int i = 0; i < register_count; i += 1) {
    Register r = Register(i);
    int32_t operand = r.ToOperand();
    uint8_t index = static_cast<uint8_t>(operand);
    CHECK_LT(index, operand_count.size());
    operand_count[index] += 1;
    CHECK_EQ(operand_count[index], 1);
  }

  for (int i = 0; i < parameter_count; i += 1) {
    Register r = Register::FromParameterIndex(i);
    uint32_t operand = r.ToOperand();
    uint8_t index = static_cast<uint8_t>(operand);
    CHECK_LT(index, operand_count.size());
    operand_count[index] += 1;
    CHECK_EQ(operand_count[index], 1);
  }
}

TEST(OperandScaling, ScalableAndNonScalable) {
  const OperandScale kOperandScales[] = {
#define VALUE(Name, _) OperandScale::k##Name,
      OPERAND_SCALE_LIST(VALUE)
#undef VALUE
  };

  for (OperandScale operand_scale : kOperandScales) {
    int scale = static_cast<int>(operand_scale);
    CHECK_EQ(Bytecodes::Size(Bytecode::kCallRuntime, operand_scale),
             1 + 2 + 2 * scale);
    CHECK_EQ(Bytecodes::Size(Bytecode::kCreateObjectLiteral, operand_scale),
             1 + 2 * scale + 1);
    CHECK_EQ(Bytecodes::Size(Bytecode::kTestIn, operand_scale), 1 + 2 * scale);
  }
}

TEST(Bytecodes, RegisterOperands) {
  CHECK(Bytecodes::IsRegisterOperandType(OperandType::kReg));
  CHECK(Bytecodes::IsRegisterOperandType(OperandType::kRegPair));
  CHECK(Bytecodes::IsRegisterInputOperandType(OperandType::kReg));
  CHECK(Bytecodes::IsRegisterInputOperandType(OperandType::kRegPair));
  CHECK(Bytecodes::IsRegisterInputOperandType(OperandType::kRegList));
  CHECK(!Bytecodes::IsRegisterOutputOperandType(OperandType::kReg));
  CHECK(!Bytecodes::IsRegisterInputOperandType(OperandType::kRegOut));
  CHECK(Bytecodes::IsRegisterOutputOperandType(OperandType::kRegOut));
  CHECK(Bytecodes::IsRegisterOutputOperandType(OperandType::kRegOutPair));
}

TEST(Bytecodes, DebugBreakExistForEachBytecode) {
  static const OperandScale kOperandScale = OperandScale::kSingle;
#define CHECK_DEBUG_BREAK_SIZE(Name, ...)                                  \
  if (!Bytecodes::IsDebugBreak(Bytecode::k##Name) &&                       \
      !Bytecodes::IsPrefixScalingBytecode(Bytecode::k##Name)) {            \
    Bytecode debug_bytecode = Bytecodes::GetDebugBreak(Bytecode::k##Name); \
    CHECK_EQ(Bytecodes::Size(Bytecode::k##Name, kOperandScale),            \
             Bytecodes::Size(debug_bytecode, kOperandScale));              \
  }
  BYTECODE_LIST(CHECK_DEBUG_BREAK_SIZE)
#undef CHECK_DEBUG_BREAK_SIZE
}

TEST(Bytecodes, DebugBreakForPrefixBytecodes) {
  CHECK_EQ(Bytecode::kDebugBreakWide,
           Bytecodes::GetDebugBreak(Bytecode::kWide));
  CHECK_EQ(Bytecode::kDebugBreakExtraWide,
           Bytecodes::GetDebugBreak(Bytecode::kExtraWide));
}

TEST(Bytecodes, PrefixMappings) {
  Bytecode prefixes[] = {Bytecode::kWide, Bytecode::kExtraWide};
  TRACED_FOREACH(Bytecode, prefix, prefixes) {
    CHECK_EQ(prefix, Bytecodes::OperandScaleToPrefixBytecode(
                         Bytecodes::PrefixBytecodeToOperandScale(prefix)));
  }
}

TEST(Bytecodes, ScaleForSignedOperand) {
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(0), OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMaxInt8), OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMinInt8), OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMaxInt8 + 1),
           OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMinInt8 - 1),
           OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMaxInt16), OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMinInt16), OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMaxInt16 + 1),
           OperandScale::kQuadruple);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMinInt16 - 1),
           OperandScale::kQuadruple);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMaxInt), OperandScale::kQuadruple);
  CHECK_EQ(Bytecodes::ScaleForSignedOperand(kMinInt), OperandScale::kQuadruple);
}

TEST(Bytecodes, ScaleForUnsignedOperands) {
  // int overloads
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(0), OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(kMaxUInt8),
           OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(kMaxUInt8 + 1),
           OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(kMaxUInt16),
           OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(kMaxUInt16 + 1),
           OperandScale::kQuadruple);
  // size_t overloads
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(static_cast<size_t>(0)),
           OperandScale::kSingle);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(static_cast<size_t>(kMaxUInt8)),
           OperandScale::kSingle);
  CHECK(Bytecodes::ScaleForUnsignedOperand(
            static_cast<size_t>(kMaxUInt8 + 1)) == OperandScale::kDouble);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(static_cast<size_t>(kMaxUInt16)),
           OperandScale::kDouble);
  CHECK(Bytecodes::ScaleForUnsignedOperand(
            static_cast<size_t>(kMaxUInt16 + 1)) == OperandScale::kQuadruple);
  CHECK_EQ(Bytecodes::ScaleForUnsignedOperand(static_cast<size_t>(kMaxUInt32)),
           OperandScale::kQuadruple);
}

TEST(Bytecodes, SizesForUnsignedOperands) {
  // int overloads
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(0), OperandSize::kByte);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(kMaxUInt8), OperandSize::kByte);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(kMaxUInt8 + 1),
           OperandSize::kShort);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(kMaxUInt16), OperandSize::kShort);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(kMaxUInt16 + 1),
           OperandSize::kQuad);
  // size_t overloads
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(static_cast<size_t>(0)),
           OperandSize::kByte);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(static_cast<size_t>(kMaxUInt8)),
           OperandSize::kByte);
  CHECK_EQ(
      Bytecodes::SizeForUnsignedOperand(static_cast<size_t>(kMaxUInt8 + 1)),
      OperandSize::kShort);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(static_cast<size_t>(kMaxUInt16)),
           OperandSize::kShort);
  CHECK(Bytecodes::SizeForUnsignedOperand(
            static_cast<size_t>(kMaxUInt16 + 1)) == OperandSize::kQuad);
  CHECK_EQ(Bytecodes::SizeForUnsignedOperand(static_cast<size_t>(kMaxUInt32)),
           OperandSize::kQuad);
}

// Helper macros to generate a check for if a bytecode is in a macro list of
// bytecodes. We can use these to exhaustively test a check over all bytecodes,
// both those that should pass and those that should fail the check.
#define OR_IS_BYTECODE(Name, ...) || bytecode == Bytecode::k##Name
#define IN_BYTECODE_LIST(BYTECODE, LIST) \
  ([](Bytecode bytecode) { return false LIST(OR_IS_BYTECODE); }(BYTECODE))

TEST(Bytecodes, IsJump) {
#define TEST_BYTECODE(Name, ...)                                 \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsJump(Bytecode::k##Name));           \
  } else {                                                       \
    EXPECT_FALSE(Bytecodes::IsJump(Bytecode::k##Name));          \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsForwardJump) {
#define TEST_BYTECODE(Name, ...)                                         \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_FORWARD_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsForwardJump(Bytecode::k##Name));            \
  } else {                                                               \
    EXPECT_FALSE(Bytecodes::IsForwardJump(Bytecode::k##Name));           \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsConditionalJump) {
#define TEST_BYTECODE(Name, ...)                                             \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_CONDITIONAL_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsConditionalJump(Bytecode::k##Name));            \
  } else {                                                                   \
    EXPECT_FALSE(Bytecodes::IsConditionalJump(Bytecode::k##Name));           \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsUnconditionalJump) {
#define TEST_BYTECODE(Name, ...)                                               \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_UNCONDITIONAL_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsUnconditionalJump(Bytecode::k##Name));            \
  } else {                                                                     \
    EXPECT_FALSE(Bytecodes::IsUnconditionalJump(Bytecode::k##Name));           \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsJumpImmediate) {
#define TEST_BYTECODE(Name, ...)                                           \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_IMMEDIATE_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsJumpImmediate(Bytecode::k##Name));            \
  } else {                                                                 \
    EXPECT_FALSE(Bytecodes::IsJumpImmediate(Bytecode::k##Name));           \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsJumpConstant) {
#define TEST_BYTECODE(Name, ...)                                          \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_CONSTANT_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsJumpConstant(Bytecode::k##Name));            \
  } else {                                                                \
    EXPECT_FALSE(Bytecodes::IsJumpConstant(Bytecode::k##Name));           \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsConditionalJumpImmediate) {
#define TEST_BYTECODE(Name, ...)                                             \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_CONDITIONAL_BYTECODE_LIST) && \
      IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_IMMEDIATE_BYTECODE_LIST)) {   \
    EXPECT_TRUE(Bytecodes::IsConditionalJumpImmediate(Bytecode::k##Name));   \
  } else {                                                                   \
    EXPECT_FALSE(Bytecodes::IsConditionalJumpImmediate(Bytecode::k##Name));  \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsConditionalJumpConstant) {
#define TEST_BYTECODE(Name, ...)                                             \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_CONDITIONAL_BYTECODE_LIST) && \
      IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_CONSTANT_BYTECODE_LIST)) {    \
    EXPECT_TRUE(Bytecodes::IsConditionalJumpConstant(Bytecode::k##Name));    \
  } else {                                                                   \
    EXPECT_FALSE(Bytecodes::IsConditionalJumpConstant(Bytecode::k##Name));   \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

TEST(Bytecodes, IsJumpIfToBoolean) {
#define TEST_BYTECODE(Name, ...)                                            \
  if (IN_BYTECODE_LIST(Bytecode::k##Name, JUMP_TO_BOOLEAN_BYTECODE_LIST)) { \
    EXPECT_TRUE(Bytecodes::IsJumpIfToBoolean(Bytecode::k##Name));           \
  } else {                                                                  \
    EXPECT_FALSE(Bytecodes::IsJumpIfToBoolean(Bytecode::k##Name));          \
  }

  BYTECODE_LIST(TEST_BYTECODE)
#undef TEST_BYTECODE
}

#undef OR_IS_BYTECODE
#undef IN_BYTECODE_LIST

TEST(OperandScale, PrefixesRequired) {
  CHECK(!Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kSingle));
  CHECK(Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kDouble));
  CHECK(
      Bytecodes::OperandScaleRequiresPrefixBytecode(OperandScale::kQuadruple));
  CHECK_EQ(Bytecodes::OperandScaleToPrefixBytecode(OperandScale::kDouble),
           Bytecode::kWide);
  CHECK_EQ(Bytecodes::OperandScaleToPrefixBytecode(OperandScale::kQuadruple),
           Bytecode::kExtraWide);
}

TEST(ImplicitRegisterUse, LogicalOperators) {
  CHECK_EQ(ImplicitRegisterUse::kNone | ImplicitRegisterUse::kReadAccumulator,
           ImplicitRegisterUse::kReadAccumulator);
  CHECK_EQ(ImplicitRegisterUse::kReadAccumulator |
               ImplicitRegisterUse::kWriteAccumulator,
           ImplicitRegisterUse::kReadWriteAccumulator);
  CHECK_EQ(ImplicitRegisterUse::kReadAccumulator &
               ImplicitRegisterUse::kReadWriteAccumulator,
           ImplicitRegisterUse::kReadAccumulator);
  CHECK_EQ(ImplicitRegisterUse::kReadAccumulator &
               ImplicitRegisterUse::kWriteAccumulator,
           ImplicitRegisterUse::kNone);
}

TEST(ImplicitRegisterUse, SampleBytecodes) {
  CHECK(Bytecodes::ReadsAccumulator(Bytecode::kStar));
  CHECK(!Bytecodes::WritesAccumulator(Bytecode::kStar));
  CHECK_EQ(Bytecodes::GetImplicitRegisterUse(Bytecode::kStar),
           ImplicitRegisterUse::kReadAccumulator);
  CHECK(!Bytecodes::ReadsAccumulator(Bytecode::kLdar));
  CHECK(Bytecodes::WritesAccumulator(Bytecode::kLdar));
  CHECK_EQ(Bytecodes::GetImplicitRegisterUse(Bytecode::kLdar),
           ImplicitRegisterUse::kWriteAccumulator);
  CHECK(Bytecodes::ReadsAccumulator(Bytecode::kAdd));
  CHECK(Bytecodes::WritesAccumulator(Bytecode::kAdd));
  CHECK_EQ(Bytecodes::GetImplicitRegisterUse(Bytecode::kAdd),
           ImplicitRegisterUse::kReadWriteAccumulator);
}

TEST(TypeOfLiteral, OnlyUndefinedGreaterThanU) {
#define CHECK_LITERAL(Name, name)                     \
  if (TestTypeOfFlags::LiteralFlag::k##Name ==        \
      TestTypeOfFlags::LiteralFlag::kUndefined) {     \
    CHECK_GT(strcmp(#name, "u"), 0);                  \
  } else if (TestTypeOfFlags::LiteralFlag::k##Name != \
             TestTypeOfFlags::LiteralFlag::kOther) {  \
    CHECK_LT(strcmp(#name, "u"), 0);                  \
  }
  TYPEOF_LITERAL_LIST(CHECK_LITERAL);
#undef CHECK_LITERAL
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

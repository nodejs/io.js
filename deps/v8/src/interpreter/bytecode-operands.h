// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_OPERANDS_H_
#define V8_INTERPRETER_BYTECODE_OPERANDS_H_

#include "src/base/bounds.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace interpreter {

#define INVALID_OPERAND_TYPE_LIST(V) V(None, OperandTypeInfo::kNone)

#define REGISTER_INPUT_OPERAND_TYPE_LIST(V)        \
  V(Reg, OperandTypeInfo::kScalableSignedByte)     \
  V(RegList, OperandTypeInfo::kScalableSignedByte) \
  V(RegPair, OperandTypeInfo::kScalableSignedByte)

#define REGISTER_OUTPUT_OPERAND_TYPE_LIST(V)          \
  V(RegOut, OperandTypeInfo::kScalableSignedByte)     \
  V(RegOutList, OperandTypeInfo::kScalableSignedByte) \
  V(RegOutPair, OperandTypeInfo::kScalableSignedByte) \
  V(RegOutTriple, OperandTypeInfo::kScalableSignedByte)

#define SIGNED_SCALABLE_SCALAR_OPERAND_TYPE_LIST(V) \
  V(Imm, OperandTypeInfo::kScalableSignedByte)

#define UNSIGNED_SCALABLE_SCALAR_OPERAND_TYPE_LIST(V) \
  V(Idx, OperandTypeInfo::kScalableUnsignedByte)      \
  V(UImm, OperandTypeInfo::kScalableUnsignedByte)     \
  V(RegCount, OperandTypeInfo::kScalableUnsignedByte)

#define UNSIGNED_FIXED_SCALAR_OPERAND_TYPE_LIST(V)    \
  V(Flag8, OperandTypeInfo::kFixedUnsignedByte)       \
  V(Flag16, OperandTypeInfo::kFixedUnsignedShort)     \
  V(IntrinsicId, OperandTypeInfo::kFixedUnsignedByte) \
  V(RuntimeId, OperandTypeInfo::kFixedUnsignedShort)  \
  V(NativeContextIndex, OperandTypeInfo::kFixedUnsignedByte)

// Carefully ordered for operand type range checks below.
#define NON_REGISTER_OPERAND_TYPE_LIST(V)       \
  INVALID_OPERAND_TYPE_LIST(V)                  \
  UNSIGNED_FIXED_SCALAR_OPERAND_TYPE_LIST(V)    \
  UNSIGNED_SCALABLE_SCALAR_OPERAND_TYPE_LIST(V) \
  SIGNED_SCALABLE_SCALAR_OPERAND_TYPE_LIST(V)

// Carefully ordered for operand type range checks below.
#define REGISTER_OPERAND_TYPE_LIST(V)  \
  REGISTER_INPUT_OPERAND_TYPE_LIST(V)  \
  REGISTER_OUTPUT_OPERAND_TYPE_LIST(V) \
  V(RegInOut, OperandTypeInfo::kScalableSignedByte)

// The list of operand types used by bytecodes.
// Carefully ordered for operand type range checks below.
#define OPERAND_TYPE_LIST(V)        \
  NON_REGISTER_OPERAND_TYPE_LIST(V) \
  REGISTER_OPERAND_TYPE_LIST(V)

// Enumeration of scaling factors applicable to scalable operands. Code
// relies on being able to cast values to integer scaling values.
#define OPERAND_SCALE_LIST(V) \
  V(Single, 1)                \
  V(Double, 2)                \
  V(Quadruple, 4)

enum class OperandScale : uint8_t {
#define DECLARE_OPERAND_SCALE(Name, Scale) k##Name = Scale,
  OPERAND_SCALE_LIST(DECLARE_OPERAND_SCALE)
#undef DECLARE_OPERAND_SCALE
      kLast = kQuadruple
};

// Enumeration of the size classes of operand types used by
// bytecodes. Code relies on being able to cast values to integer
// types to get the size in bytes.
enum class OperandSize : uint8_t {
  kNone = 0,
  kByte = 1,
  kShort = 2,
  kQuad = 4,
  kLast = kQuad
};

// Primitive operand info used that summarize properties of operands.
// Columns are Name, IsScalable, IsUnsigned, UnscaledSize.
#define OPERAND_TYPE_INFO_LIST(V)                         \
  V(None, false, false, OperandSize::kNone)               \
  V(ScalableSignedByte, true, false, OperandSize::kByte)  \
  V(ScalableUnsignedByte, true, true, OperandSize::kByte) \
  V(FixedUnsignedByte, false, true, OperandSize::kByte)   \
  V(FixedUnsignedShort, false, true, OperandSize::kShort)

enum class OperandTypeInfo : uint8_t {
#define DECLARE_OPERAND_TYPE_INFO(Name, ...) k##Name,
  OPERAND_TYPE_INFO_LIST(DECLARE_OPERAND_TYPE_INFO)
#undef DECLARE_OPERAND_TYPE_INFO
};

// Enumeration of operand types used by bytecodes.
enum class OperandType : uint8_t {
#define DECLARE_OPERAND_TYPE(Name, _) k##Name,
  OPERAND_TYPE_LIST(DECLARE_OPERAND_TYPE)
#undef DECLARE_OPERAND_TYPE
#define COUNT_OPERAND_TYPES(x, _) +1
  // The COUNT_OPERAND macro will turn this into kLast = -1 +1 +1... which will
  // evaluate to the same value as the last operand.
  kLast = -1 OPERAND_TYPE_LIST(COUNT_OPERAND_TYPES)
#undef COUNT_OPERAND_TYPES
};

enum class ImplicitRegisterUse : uint8_t {
  kNone = 0,
  kReadAccumulator = 1 << 0,
  kWriteAccumulator = 1 << 1,
  kClobberAccumulator = 1 << 2,
  kWriteShortStar = 1 << 3,
  kReadWriteAccumulator = kReadAccumulator | kWriteAccumulator,
  kReadAndClobberAccumulator = kReadAccumulator | kClobberAccumulator,
  kReadAccumulatorWriteShortStar = kReadAccumulator | kWriteShortStar
};

constexpr inline ImplicitRegisterUse operator&(ImplicitRegisterUse lhs,
                                               ImplicitRegisterUse rhs) {
  return static_cast<ImplicitRegisterUse>(static_cast<int>(lhs) &
                                          static_cast<int>(rhs));
}

constexpr inline ImplicitRegisterUse operator|(ImplicitRegisterUse lhs,
                                               ImplicitRegisterUse rhs) {
  return static_cast<ImplicitRegisterUse>(static_cast<int>(lhs) |
                                          static_cast<int>(rhs));
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const ImplicitRegisterUse& use);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const OperandScale& operand_scale);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const OperandSize& operand_size);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const OperandType& operand_type);

class BytecodeOperands : public AllStatic {
 public:
  // The total number of bytecode operand types used.
  static const int kOperandTypeCount = static_cast<int>(OperandType::kLast) + 1;

// The total number of bytecode operand scales used.
#define OPERAND_SCALE_COUNT(...) +1
  static const int kOperandScaleCount =
      0 OPERAND_SCALE_LIST(OPERAND_SCALE_COUNT);
#undef OPERAND_SCALE_COUNT

  static constexpr int OperandScaleAsIndex(OperandScale operand_scale) {
#ifdef DEBUG
    int result = static_cast<int>(operand_scale) >> 1;
    switch (operand_scale) {
      case OperandScale::kSingle:
        DCHECK_EQ(0, result);
        break;
      case OperandScale::kDouble:
        DCHECK_EQ(1, result);
        break;
      case OperandScale::kQuadruple:
        DCHECK_EQ(2, result);
        break;
      default:
        UNREACHABLE();
    }
#endif
    return static_cast<int>(operand_scale) >> 1;
  }

  // Returns true if |implicit_register_use| reads the
  // accumulator.
  static constexpr bool ReadsAccumulator(
      ImplicitRegisterUse implicit_register_use) {
    return (implicit_register_use & ImplicitRegisterUse::kReadAccumulator) ==
           ImplicitRegisterUse::kReadAccumulator;
  }

  // Returns true if |implicit_register_use| writes the
  // accumulator.
  static constexpr bool WritesAccumulator(
      ImplicitRegisterUse implicit_register_use) {
    return (implicit_register_use & ImplicitRegisterUse::kWriteAccumulator) ==
           ImplicitRegisterUse::kWriteAccumulator;
  }

  // Returns true if |implicit_register_use| clobbers the
  // accumulator.
  static constexpr bool ClobbersAccumulator(
      ImplicitRegisterUse implicit_register_use) {
    return (implicit_register_use & ImplicitRegisterUse::kClobberAccumulator) ==
           ImplicitRegisterUse::kClobberAccumulator;
  }

  // Returns true if |implicit_register_use| writes or clobbers the
  // accumulator.
  static constexpr bool WritesOrClobbersAccumulator(
      ImplicitRegisterUse implicit_register_use) {
    return (implicit_register_use &
            (ImplicitRegisterUse::kWriteAccumulator |
             ImplicitRegisterUse::kClobberAccumulator)) !=
           ImplicitRegisterUse::kNone;
  }

  // Returns true if |implicit_register_use| writes to a
  // register not specified by an operand.
  static constexpr bool WritesImplicitRegister(
      ImplicitRegisterUse implicit_register_use) {
    return (implicit_register_use & ImplicitRegisterUse::kWriteShortStar) ==
           ImplicitRegisterUse::kWriteShortStar;
  }

  // Returns true if |operand_type| is a scalable signed byte.
  static constexpr bool IsScalableSignedByte(OperandType operand_type) {
    return base::IsInRange(operand_type, OperandType::kImm,
                           OperandType::kRegInOut);
  }

  // Returns true if |operand_type| is a scalable unsigned byte.
  static constexpr bool IsScalableUnsignedByte(OperandType operand_type) {
    return base::IsInRange(operand_type, OperandType::kIdx,
                           OperandType::kRegCount);
  }
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_OPERANDS_H_

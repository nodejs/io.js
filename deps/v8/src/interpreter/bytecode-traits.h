// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_TRAITS_H_
#define V8_INTERPRETER_BYTECODE_TRAITS_H_

#include "src/interpreter/bytecode-operands.h"

namespace v8 {
namespace internal {
namespace interpreter {

template <OperandTypeInfo>
struct OperandTypeInfoTraits;

#define DECLARE_OPERAND_TYPE_INFO(Name, Scalable, Unsigned, BaseSize) \
  template <>                                                         \
  struct OperandTypeInfoTraits<OperandTypeInfo::k##Name> {            \
    static constexpr bool kIsScalable = Scalable;                     \
    static constexpr bool kIsUnsigned = Unsigned;                     \
    static constexpr OperandSize kUnscaledSize = BaseSize;            \
  };
OPERAND_TYPE_INFO_LIST(DECLARE_OPERAND_TYPE_INFO)
#undef DECLARE_OPERAND_TYPE_INFO

template <OperandType>
struct OperandTraits;

#define DECLARE_OPERAND_TYPE_TRAITS(Name, InfoType)               \
  template <>                                                     \
  struct OperandTraits<OperandType::k##Name> {                    \
    using TypeInfoTraits = OperandTypeInfoTraits<InfoType>;       \
    static constexpr OperandTypeInfo kOperandTypeInfo = InfoType; \
  };
OPERAND_TYPE_LIST(DECLARE_OPERAND_TYPE_TRAITS)
#undef DECLARE_OPERAND_TYPE_TRAITS

template <OperandType operand_type, OperandScale operand_scale>
struct OperandScaler {
  static constexpr int kSize =
      static_cast<int>(
          OperandTraits<operand_type>::TypeInfoTraits::kUnscaledSize) *
      (OperandTraits<operand_type>::TypeInfoTraits::kIsScalable
           ? static_cast<int>(operand_scale)
           : 1);
  static constexpr OperandSize kOperandSize = static_cast<OperandSize>(kSize);
};

template <ImplicitRegisterUse implicit_register_use, OperandType... operands>
struct BytecodeTraits {
  static constexpr OperandType kOperandTypes[] = {operands...};
  static constexpr OperandTypeInfo kOperandTypeInfos[] = {
      OperandTraits<operands>::kOperandTypeInfo...};

  static constexpr OperandSize kSingleScaleOperandSizes[] = {
      OperandScaler<operands, OperandScale::kSingle>::kOperandSize...};
  static constexpr OperandSize kDoubleScaleOperandSizes[] = {
      OperandScaler<operands, OperandScale::kDouble>::kOperandSize...};
  static constexpr OperandSize kQuadrupleScaleOperandSizes[] = {
      OperandScaler<operands, OperandScale::kQuadruple>::kOperandSize...};

  template <OperandScale scale>
  static constexpr auto CalculateOperandOffsets() {
    std::array<int, sizeof...(operands) + 1> result{};
    int offset = 1;
    int i = 0;
    (((result[i++] = offset),
      (offset += OperandScaler<operands, scale>::kSize)),
     ...);
    return result;
  }

  static constexpr auto kSingleScaleOperandOffsets =
      CalculateOperandOffsets<OperandScale::kSingle>();
  static constexpr auto kDoubleScaleOperandOffsets =
      CalculateOperandOffsets<OperandScale::kDouble>();
  static constexpr auto kQuadrupleScaleOperandOffsets =
      CalculateOperandOffsets<OperandScale::kQuadruple>();

  static constexpr int kSingleScaleSize =
      (1 + ... + OperandScaler<operands, OperandScale::kSingle>::kSize);
  static constexpr int kDoubleScaleSize =
      (1 + ... + OperandScaler<operands, OperandScale::kDouble>::kSize);
  static constexpr int kQuadrupleScaleSize =
      (1 + ... + OperandScaler<operands, OperandScale::kQuadruple>::kSize);

  static constexpr ImplicitRegisterUse kImplicitRegisterUse =
      implicit_register_use;
  static constexpr int kOperandCount = sizeof...(operands);
};

template <ImplicitRegisterUse implicit_register_use>
struct BytecodeTraits<implicit_register_use> {
  static constexpr OperandType* kOperandTypes = nullptr;
  static constexpr OperandTypeInfo* kOperandTypeInfos = nullptr;
  static constexpr OperandSize* kSingleScaleOperandSizes = nullptr;
  static constexpr OperandSize* kDoubleScaleOperandSizes = nullptr;
  static constexpr OperandSize* kQuadrupleScaleOperandSizes = nullptr;

  static constexpr auto kSingleScaleOperandOffsets = std::array<int, 0>{};
  static constexpr auto kDoubleScaleOperandOffsets = std::array<int, 0>{};
  static constexpr auto kQuadrupleScaleOperandOffsets = std::array<int, 0>{};

  static constexpr int kSingleScaleSize = 1;
  static constexpr int kDoubleScaleSize = 1;
  static constexpr int kQuadrupleScaleSize = 1;
  static constexpr ImplicitRegisterUse kImplicitRegisterUse =
      implicit_register_use;
  static constexpr int kOperandCount = 0;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_TRAITS_H_

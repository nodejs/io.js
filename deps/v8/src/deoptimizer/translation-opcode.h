// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_TRANSLATION_OPCODE_H_
#define V8_DEOPTIMIZER_TRANSLATION_OPCODE_H_

#include "src/base/macros.h"

namespace v8 {
namespace internal {

// V(name, operand_count)
#define TRANSLATION_JS_FRAME_OPCODE_LIST(V)    \
  V(INTERPRETED_FRAME_WITH_RETURN, 5)          \
  V(INTERPRETED_FRAME_WITHOUT_RETURN, 3)       \
  V(JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME, 3) \
  V(JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME, 3)

#define TRANSLATION_FRAME_OPCODE_LIST(V)               \
  V(CONSTRUCT_CREATE_STUB_FRAME, 2)                    \
  V(CONSTRUCT_INVOKE_STUB_FRAME, 1)                    \
  V(BUILTIN_CONTINUATION_FRAME, 3)                     \
  IF_WASM(V, JS_TO_WASM_BUILTIN_CONTINUATION_FRAME, 4) \
  IF_WASM(V, WASM_INLINED_INTO_JS_FRAME, 3)            \
  IF_WASM(V, LIFTOFF_FRAME, 3)                         \
  V(INLINED_EXTRA_ARGUMENTS, 2)

#define TRANSLATION_OPCODE_LIST(V)    \
  TRANSLATION_JS_FRAME_OPCODE_LIST(V) \
  TRANSLATION_FRAME_OPCODE_LIST(V)    \
  V(ARGUMENTS_ELEMENTS, 1)            \
  V(ARGUMENTS_LENGTH, 0)              \
  V(REST_LENGTH, 0)                   \
  V(BEGIN_WITHOUT_FEEDBACK, 3)        \
  V(BEGIN_WITH_FEEDBACK, 3)           \
  V(BOOL_REGISTER, 1)                 \
  V(BOOL_STACK_SLOT, 1)               \
  V(CAPTURED_OBJECT, 1)               \
  V(DOUBLE_REGISTER, 1)               \
  V(DOUBLE_STACK_SLOT, 1)             \
  V(SIMD128_STACK_SLOT, 1)            \
  V(HOLEY_DOUBLE_REGISTER, 1)         \
  V(HOLEY_DOUBLE_STACK_SLOT, 1)       \
  V(SIMD128_REGISTER, 1)              \
  V(DUPLICATED_OBJECT, 1)             \
  V(FLOAT_REGISTER, 1)                \
  V(FLOAT_STACK_SLOT, 1)              \
  V(INT32_REGISTER, 1)                \
  V(INT32_STACK_SLOT, 1)              \
  V(INT64_REGISTER, 1)                \
  V(INT64_STACK_SLOT, 1)              \
  V(SIGNED_BIGINT64_REGISTER, 1)      \
  V(SIGNED_BIGINT64_STACK_SLOT, 1)    \
  V(UNSIGNED_BIGINT64_REGISTER, 1)    \
  V(UNSIGNED_BIGINT64_STACK_SLOT, 1)  \
  V(OPTIMIZED_OUT, 0)                 \
  V(LITERAL, 1)                       \
  V(REGISTER, 1)                      \
  V(TAGGED_STACK_SLOT, 1)             \
  V(UINT32_REGISTER, 1)               \
  V(UINT32_STACK_SLOT, 1)             \
  V(UPDATE_FEEDBACK, 2)               \
  V(MATCH_PREVIOUS_TRANSLATION, 1)

enum class TranslationOpcode {
#define CASE(name, ...) name,
  TRANSLATION_OPCODE_LIST(CASE)
#undef CASE
};

#define PLUS_ONE(...) +1
static constexpr int kNumTranslationOpcodes =
    0 TRANSLATION_OPCODE_LIST(PLUS_ONE);
static constexpr int kNumTranslationJsFrameOpcodes =
    0 TRANSLATION_JS_FRAME_OPCODE_LIST(PLUS_ONE);
static constexpr int kNumTranslationFrameOpcodes =
    kNumTranslationJsFrameOpcodes TRANSLATION_FRAME_OPCODE_LIST(PLUS_ONE);
#undef PLUS_ONE

inline int TranslationOpcodeOperandCount(TranslationOpcode o) {
#define CASE(name, operand_count) operand_count,
  static const int counts[] = {TRANSLATION_OPCODE_LIST(CASE)};
#undef CASE
  return counts[static_cast<int>(o)];
}

constexpr int kMaxTranslationOperandCount = 5;
#define CASE(name, operand_count) \
  static_assert(operand_count <= kMaxTranslationOperandCount);
TRANSLATION_OPCODE_LIST(CASE)
#undef CASE

inline bool TranslationOpcodeIsBegin(TranslationOpcode o) {
  return o == TranslationOpcode::BEGIN_WITH_FEEDBACK ||
         o == TranslationOpcode::BEGIN_WITHOUT_FEEDBACK;
}

inline bool IsTranslationFrameOpcode(TranslationOpcode o) {
  static_assert(
      0 == static_cast<int>(TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN));
  static_assert(kNumTranslationJsFrameOpcodes < kNumTranslationFrameOpcodes);
  return static_cast<int>(o) < kNumTranslationFrameOpcodes;
}

inline bool IsTranslationJsFrameOpcode(TranslationOpcode o) {
  static_assert(
      kNumTranslationJsFrameOpcodes ==
      static_cast<int>(TranslationOpcode::CONSTRUCT_CREATE_STUB_FRAME));
  return static_cast<int>(o) < kNumTranslationJsFrameOpcodes;
}

inline bool IsTranslationInterpreterFrameOpcode(TranslationOpcode o) {
  return o == TranslationOpcode::INTERPRETED_FRAME_WITH_RETURN ||
         o == TranslationOpcode::INTERPRETED_FRAME_WITHOUT_RETURN;
}

inline std::ostream& operator<<(std::ostream& out, TranslationOpcode opcode) {
  switch (opcode) {
#define CASE(name, _)           \
  case TranslationOpcode::name: \
    out << #name;               \
    break;
    TRANSLATION_OPCODE_LIST(CASE)
#undef CASE
    default:
      out << "BROKEN_OPCODE_" << static_cast<uint8_t>(opcode);
      break;
  }
  return out;
}

#undef TRANSLATION_OPCODE_LIST
#undef TRANSLATION_FRAME_OPCODE_LIST

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_TRANSLATION_OPCODE_H_

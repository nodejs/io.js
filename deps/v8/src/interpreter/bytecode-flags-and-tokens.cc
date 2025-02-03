// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-flags-and-tokens.h"

#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

// static
uint8_t CreateArrayLiteralFlags::Encode(bool use_fast_shallow_clone,
                                        int runtime_flags) {
  uint8_t result = FlagsBits::encode(runtime_flags);
  result |= FastCloneSupportedBit::encode(use_fast_shallow_clone);
  return result;
}

// static
uint8_t CreateObjectLiteralFlags::Encode(int runtime_flags,
                                         bool fast_clone_supported) {
  uint8_t result = FlagsBits::encode(runtime_flags);
  result |= FastCloneSupportedBit::encode(fast_clone_supported);
  return result;
}

// static
uint8_t CreateClosureFlags::Encode(bool pretenure, bool is_function_scope,
                                   bool might_always_turbofan) {
  uint8_t result = PretenuredBit::encode(pretenure);
  if (!might_always_turbofan && !pretenure && is_function_scope) {
    result |= FastNewClosureBit::encode(true);
  }
  return result;
}

// static
TestTypeOfFlags::LiteralFlag TestTypeOfFlags::GetFlagForLiteral(
    const AstStringConstants* ast_constants, Literal* literal) {
  const AstRawString* raw_literal = literal->AsRawString();
  if (raw_literal == ast_constants->number_string()) {
    return LiteralFlag::kNumber;
  } else if (raw_literal == ast_constants->string_string()) {
    return LiteralFlag::kString;
  } else if (raw_literal == ast_constants->symbol_string()) {
    return LiteralFlag::kSymbol;
  } else if (raw_literal == ast_constants->boolean_string()) {
    return LiteralFlag::kBoolean;
  } else if (raw_literal == ast_constants->bigint_string()) {
    return LiteralFlag::kBigInt;
  } else if (raw_literal == ast_constants->undefined_string()) {
    return LiteralFlag::kUndefined;
  } else if (raw_literal == ast_constants->function_string()) {
    return LiteralFlag::kFunction;
  } else if (raw_literal == ast_constants->object_string()) {
    return LiteralFlag::kObject;
  } else {
    return LiteralFlag::kOther;
  }
}

// static
uint8_t TestTypeOfFlags::Encode(LiteralFlag literal_flag) {
  return static_cast<uint8_t>(literal_flag);
}

// static
TestTypeOfFlags::LiteralFlag TestTypeOfFlags::Decode(uint8_t raw_flag) {
  DCHECK_LE(raw_flag, static_cast<uint8_t>(LiteralFlag::kOther));
  return static_cast<LiteralFlag>(raw_flag);
}

// static
const char* TestTypeOfFlags::ToString(LiteralFlag literal_flag) {
  switch (literal_flag) {
#define CASE(Name, name)     \
  case LiteralFlag::k##Name: \
    return #name;
    TYPEOF_LITERAL_LIST(CASE)
#undef CASE
    default:
      return "<invalid>";
  }
}

// static
uint8_t StoreLookupSlotFlags::Encode(LanguageMode language_mode,
                                     LookupHoistingMode lookup_hoisting_mode) {
  DCHECK_IMPLIES(lookup_hoisting_mode == LookupHoistingMode::kLegacySloppy,
                 language_mode == LanguageMode::kSloppy);
  return LanguageModeBit::encode(language_mode) |
         LookupHoistingModeBit::encode(static_cast<bool>(lookup_hoisting_mode));
}

// static
LanguageMode StoreLookupSlotFlags::GetLanguageMode(uint8_t flags) {
  return LanguageModeBit::decode(flags);
}

// static
bool StoreLookupSlotFlags::IsLookupHoistingMode(uint8_t flags) {
  return LookupHoistingModeBit::decode(flags);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

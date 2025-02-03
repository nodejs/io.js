// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_OPCODES_INL_H_
#define V8_WASM_WASM_OPCODES_INL_H_

#include <array>

#include "src/base/template-utils.h"
#include "src/codegen/signature.h"
#include "src/execution/messages.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

// static
constexpr const char* WasmOpcodes::OpcodeName(WasmOpcode opcode) {
  switch (opcode) {
#define CASE(opcode, binary, sig, name, ...) \
  case kExpr##opcode:                        \
    return name;
    FOREACH_OPCODE(CASE)
#undef CASE

    case kNumericPrefix:
    case kSimdPrefix:
    case kAtomicPrefix:
    case kGCPrefix:
      return "unknown";
  }
  // Even though the switch above handles all well-defined enum values,
  // random modules (e.g. fuzzer generated) can call this function with
  // random (invalid) opcodes. Handle those here:
  return "invalid opcode";
}

// static
constexpr bool WasmOpcodes::IsPrefixOpcode(WasmOpcode opcode) {
  switch (opcode) {
#define CHECK_PREFIX(name, opcode) case k##name##Prefix:
    FOREACH_PREFIX(CHECK_PREFIX)
#undef CHECK_PREFIX
    return true;
    default:
      return false;
  }
}

// static
constexpr bool WasmOpcodes::IsControlOpcode(WasmOpcode opcode) {
  switch (opcode) {
#define CHECK_OPCODE(name, ...) case kExpr##name:
    FOREACH_CONTROL_OPCODE(CHECK_OPCODE)
#undef CHECK_OPCODE
    return true;
    default:
      return false;
  }
}

// static
constexpr bool WasmOpcodes::IsUnconditionalJump(WasmOpcode opcode) {
  switch (opcode) {
    case kExprUnreachable:
    case kExprBr:
    case kExprBrTable:
    case kExprReturn:
    case kExprReturnCall:
    case kExprReturnCallIndirect:
    case kExprThrow:
    case kExprRethrow:
      return true;
    default:
      return false;
  }
}

// static
constexpr bool WasmOpcodes::IsBreakable(WasmOpcode opcode) {
  switch (opcode) {
    case kExprBlock:
    case kExprTry:
    case kExprCatch:
    case kExprLoop:
    case kExprElse:
      return false;
    default:
      return true;
  }
}

// static
constexpr bool WasmOpcodes::IsExternRefOpcode(WasmOpcode opcode) {
  switch (opcode) {
    case kExprRefNull:
    case kExprRefIsNull:
    case kExprRefFunc:
    case kExprRefAsNonNull:
      return true;
    default:
      return false;
  }
}

// static
constexpr bool WasmOpcodes::IsThrowingOpcode(WasmOpcode opcode) {
  // TODO(8729): Trapping opcodes are not yet considered to be throwing.
  switch (opcode) {
    case kExprThrow:
    case kExprRethrow:
    case kExprCallFunction:
    case kExprCallIndirect:
      return true;
    default:
      return false;
  }
}

// static
constexpr bool WasmOpcodes::IsRelaxedSimdOpcode(WasmOpcode opcode) {
  // Relaxed SIMD opcodes have the SIMD prefix (0xfd) shifted by 12 bits, and
  // nibble 3 must be 0x1. I.e. their encoded opcode is in [0xfd100, 0xfd1ff].
  static_assert(kSimdPrefix == 0xfd);
#define CHECK_OPCODE(name, opcode, ...) \
  static_assert((opcode & 0xfff00) == 0xfd100);
  FOREACH_RELAXED_SIMD_OPCODE(CHECK_OPCODE)
#undef CHECK_OPCODE

  return (opcode & 0xfff00) == 0xfd100;
}

constexpr bool WasmOpcodes::IsFP16SimdOpcode(WasmOpcode opcode) {
  return opcode >= kExprF16x8Splat && opcode <= kExprF16x8Qfms;
}

#if DEBUG
// static
constexpr bool WasmOpcodes::IsMemoryAccessOpcode(WasmOpcode opcode) {
  switch (opcode) {
#define MEM_OPCODE(name, ...) case WasmOpcode::kExpr##name:
    FOREACH_LOAD_MEM_OPCODE(MEM_OPCODE)
    FOREACH_STORE_MEM_OPCODE(MEM_OPCODE)
    FOREACH_ATOMIC_OPCODE(MEM_OPCODE)
    FOREACH_SIMD_MEM_OPCODE(MEM_OPCODE)
    FOREACH_SIMD_MEM_1_OPERAND_OPCODE(MEM_OPCODE)
    return true;
    default:
      return false;
  }
}
#endif  // DEBUG

constexpr uint8_t WasmOpcodes::ExtractPrefix(WasmOpcode opcode) {
  // See comment on {WasmOpcode} for the encoding.
  return (opcode > 0xffff) ? opcode >> 12 : opcode >> 8;
}

namespace impl {

#define DECLARE_SIG_ENUM(name, ...) kSigEnum_##name,
enum WasmOpcodeSig : uint8_t {
  kSigEnum_None,
  FOREACH_SIGNATURE(DECLARE_SIG_ENUM)
};
#undef DECLARE_SIG_ENUM
#define DECLARE_SIG(name, ...)                                                \
  constexpr ValueType kTypes_##name[] = {__VA_ARGS__};                        \
  constexpr int kReturnsCount_##name = kTypes_##name[0] == kWasmVoid ? 0 : 1; \
  constexpr FunctionSig kSig_##name(                                          \
      kReturnsCount_##name, static_cast<int>(arraysize(kTypes_##name)) - 1,   \
      kTypes_##name + (1 - kReturnsCount_##name));
FOREACH_SIGNATURE(DECLARE_SIG)
#undef DECLARE_SIG

#define DECLARE_SIG_ENTRY(name, ...) &kSig_##name,
constexpr const FunctionSig* kCachedSigs[] = {
    nullptr, FOREACH_SIGNATURE(DECLARE_SIG_ENTRY)};
#undef DECLARE_SIG_ENTRY

constexpr WasmOpcodeSig GetShortOpcodeSigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == opc ? kSigEnum_##sig:
  return FOREACH_SIMPLE_OPCODE(CASE) FOREACH_SIMPLE_PROTOTYPE_OPCODE(CASE)
      kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetAsmJsOpcodeSigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == opc ? kSigEnum_##sig:
  return FOREACH_ASMJS_COMPAT_OPCODE(CASE) kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetSimdOpcodeSigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == (opc & 0xFF) ? kSigEnum_##sig:
  return FOREACH_SIMD_MVP_0_OPERAND_OPCODE(CASE) FOREACH_SIMD_MEM_OPCODE(CASE)
      FOREACH_SIMD_MEM_1_OPERAND_OPCODE(CASE) kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetRelaxedSimdOpcodeSigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == (opc & 0xFF) ? kSigEnum_##sig:
  return FOREACH_RELAXED_SIMD_OPCODE(CASE) kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetAtomicOpcodeMem32SigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == (opc & 0xFF) ? kSigEnum_##sig:
  return FOREACH_ATOMIC_OPCODE(CASE) FOREACH_ATOMIC_0_OPERAND_OPCODE(CASE)
      kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetAtomicOpcodeMem64SigIndex(uint8_t opcode) {
#define CASE(name, opc, sig32, text, sig64) \
  opcode == (opc & 0xFF) ? kSigEnum_##sig64:
  return FOREACH_ATOMIC_OPCODE(CASE) FOREACH_ATOMIC_0_OPERAND_OPCODE(CASE)
      kSigEnum_None;
#undef CASE
}

constexpr WasmOpcodeSig GetNumericOpcodeSigIndex(uint8_t opcode) {
#define CASE(name, opc, sig, ...) opcode == (opc & 0xFF) ? kSigEnum_##sig:
  return FOREACH_NUMERIC_OPCODE_WITH_SIG(CASE) kSigEnum_None;
#undef CASE
}

constexpr std::array<WasmOpcodeSig, 256> kShortSigTable =
    base::make_array<256>(GetShortOpcodeSigIndex);
constexpr std::array<WasmOpcodeSig, 256> kSimpleAsmjsExprSigTable =
    base::make_array<256>(GetAsmJsOpcodeSigIndex);
constexpr std::array<WasmOpcodeSig, 256> kSimdExprSigTable =
    base::make_array<256>(GetSimdOpcodeSigIndex);
constexpr std::array<WasmOpcodeSig, 256> kRelaxedSimdExprSigTable =
    base::make_array<256>(GetRelaxedSimdOpcodeSigIndex);
constexpr std::array<WasmOpcodeSig, 256> kAtomicExprSigTableMem32 =
    base::make_array<256>(GetAtomicOpcodeMem32SigIndex);
constexpr std::array<WasmOpcodeSig, 256> kAtomicExprSigTableMem64 =
    base::make_array<256>(GetAtomicOpcodeMem64SigIndex);
constexpr std::array<WasmOpcodeSig, 256> kNumericExprSigTable =
    base::make_array<256>(GetNumericOpcodeSigIndex);

}  // namespace impl

constexpr const FunctionSig* WasmOpcodes::Signature(WasmOpcode opcode) {
  switch (ExtractPrefix(opcode)) {
    case 0:
      DCHECK_GT(impl::kShortSigTable.size(), opcode);
      return impl::kCachedSigs[impl::kShortSigTable[opcode]];
    case kSimdPrefix: {
      // Handle SIMD MVP opcodes (in [0xfd00, 0xfdff]).
      if (opcode <= 0xfdff) {
        DCHECK_LE(0xfd00, opcode);
        return impl::kCachedSigs[impl::kSimdExprSigTable[opcode & 0xff]];
      }
      // Handle relaxed SIMD opcodes (in [0xfd100, 0xfd1ff]).
      if (IsRelaxedSimdOpcode(opcode)) {
        return impl::kCachedSigs[impl::kRelaxedSimdExprSigTable[opcode & 0xff]];
      }
      return nullptr;
    }
    case kNumericPrefix:
      return impl::kCachedSigs[impl::kNumericExprSigTable[opcode & 0xff]];
    default:
      UNREACHABLE();  // invalid prefix.
  }
}

constexpr const FunctionSig* WasmOpcodes::SignatureForAtomicOp(
    WasmOpcode opcode, bool is_memory64) {
  if (is_memory64) {
    return impl::kCachedSigs[impl::kAtomicExprSigTableMem64[opcode & 0xff]];
  } else {
    return impl::kCachedSigs[impl::kAtomicExprSigTableMem32[opcode & 0xff]];
  }
}

constexpr const FunctionSig* WasmOpcodes::AsmjsSignature(WasmOpcode opcode) {
  DCHECK_GT(impl::kSimpleAsmjsExprSigTable.size(), opcode);
  return impl::kCachedSigs[impl::kSimpleAsmjsExprSigTable[opcode]];
}

constexpr MessageTemplate WasmOpcodes::TrapReasonToMessageId(
    TrapReason reason) {
  switch (reason) {
#define TRAPREASON_TO_MESSAGE(name) \
  case k##name:                     \
    return MessageTemplate::kWasm##name;
    FOREACH_WASM_TRAPREASON(TRAPREASON_TO_MESSAGE)
#undef TRAPREASON_TO_MESSAGE
    case kTrapCount:
      UNREACHABLE();
  }
}

constexpr TrapReason WasmOpcodes::MessageIdToTrapReason(
    MessageTemplate message) {
  switch (message) {
#define MESSAGE_TO_TRAPREASON(name)  \
  case MessageTemplate::kWasm##name: \
    return k##name;
    FOREACH_WASM_TRAPREASON(MESSAGE_TO_TRAPREASON)
#undef MESSAGE_TO_TRAPREASON
    default:
      UNREACHABLE();
  }
}

const char* WasmOpcodes::TrapReasonMessage(TrapReason reason) {
  return MessageFormatter::TemplateString(TrapReasonToMessageId(reason));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_OPCODES_INL_H_

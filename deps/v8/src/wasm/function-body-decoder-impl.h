// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_FUNCTION_BODY_DECODER_IMPL_H_
#define V8_WASM_FUNCTION_BODY_DECODER_IMPL_H_

// Do only include this header for implementing new Interface of the
// WasmFullDecoder.

#include <inttypes.h>

#include <optional>

#include "src/base/bounds.h"
#include "src/base/small-vector.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/strings/unicode.h"
#include "src/utils/bit-vector.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::wasm {

struct WasmGlobal;
struct WasmTag;

#define TRACE(...)                                        \
  do {                                                    \
    if (v8_flags.trace_wasm_decoder) PrintF(__VA_ARGS__); \
  } while (false)

#define TRACE_INST_FORMAT "  @%-8d #%-30s|"

// Return the evaluation of {condition} if {ValidationTag::validate} is true,
// DCHECK that it is true and always return true otherwise.
// Note that this needs to be a macro, because the "likely" annotation does not
// survive inlining.
#ifdef DEBUG
#define VALIDATE(condition)                       \
  (ValidationTag::validate ? V8_LIKELY(condition) \
                           : ValidateAssumeTrue(condition, #condition))

V8_INLINE bool ValidateAssumeTrue(bool condition, const char* message) {
  DCHECK_WITH_MSG(condition, message);
  return true;
}
#else
#define VALIDATE(condition) (!ValidationTag::validate || V8_LIKELY(condition))
#endif

#define CHECK_PROTOTYPE_OPCODE(feat)                                         \
  DCHECK(this->module_->origin == kWasmOrigin);                              \
  if (!VALIDATE(this->enabled_.has_##feat())) {                              \
    this->DecodeError(                                                       \
        "Invalid opcode 0x%02x (enable with --experimental-wasm-" #feat ")", \
        opcode);                                                             \
    return 0;                                                                \
  }                                                                          \
  this->detected_->add_##feat()

static constexpr LoadType GetLoadType(WasmOpcode opcode) {
  // Hard-code the list of load types. The opcodes are highly unlikely to
  // ever change, and we have some checks here to guard against that.
  static_assert(sizeof(LoadType) == sizeof(uint8_t), "LoadType is compact");
  constexpr uint8_t kMinOpcode = kExprI32LoadMem;
  constexpr uint8_t kMaxOpcode = kExprI64LoadMem32U;
  constexpr LoadType kLoadTypes[] = {
      LoadType::kI32Load,    LoadType::kI64Load,    LoadType::kF32Load,
      LoadType::kF64Load,    LoadType::kI32Load8S,  LoadType::kI32Load8U,
      LoadType::kI32Load16S, LoadType::kI32Load16U, LoadType::kI64Load8S,
      LoadType::kI64Load8U,  LoadType::kI64Load16S, LoadType::kI64Load16U,
      LoadType::kI64Load32S, LoadType::kI64Load32U};
  static_assert(arraysize(kLoadTypes) == kMaxOpcode - kMinOpcode + 1);
  DCHECK_LE(kMinOpcode, opcode);
  DCHECK_GE(kMaxOpcode, opcode);
  return kLoadTypes[opcode - kMinOpcode];
}

static constexpr StoreType GetStoreType(WasmOpcode opcode) {
  // Hard-code the list of store types. The opcodes are highly unlikely to
  // ever change, and we have some checks here to guard against that.
  static_assert(sizeof(StoreType) == sizeof(uint8_t), "StoreType is compact");
  constexpr uint8_t kMinOpcode = kExprI32StoreMem;
  constexpr uint8_t kMaxOpcode = kExprI64StoreMem32;
  constexpr StoreType kStoreTypes[] = {
      StoreType::kI32Store,  StoreType::kI64Store,   StoreType::kF32Store,
      StoreType::kF64Store,  StoreType::kI32Store8,  StoreType::kI32Store16,
      StoreType::kI64Store8, StoreType::kI64Store16, StoreType::kI64Store32};
  static_assert(arraysize(kStoreTypes) == kMaxOpcode - kMinOpcode + 1);
  DCHECK_LE(kMinOpcode, opcode);
  DCHECK_GE(kMaxOpcode, opcode);
  return kStoreTypes[opcode - kMinOpcode];
}

#define ATOMIC_OP_LIST(V)                \
  V(AtomicNotify, Uint32)                \
  V(I32AtomicWait, Uint32)               \
  V(I64AtomicWait, Uint64)               \
  V(I32AtomicLoad, Uint32)               \
  V(I64AtomicLoad, Uint64)               \
  V(I32AtomicLoad8U, Uint8)              \
  V(I32AtomicLoad16U, Uint16)            \
  V(I64AtomicLoad8U, Uint8)              \
  V(I64AtomicLoad16U, Uint16)            \
  V(I64AtomicLoad32U, Uint32)            \
  V(I32AtomicAdd, Uint32)                \
  V(I32AtomicAdd8U, Uint8)               \
  V(I32AtomicAdd16U, Uint16)             \
  V(I64AtomicAdd, Uint64)                \
  V(I64AtomicAdd8U, Uint8)               \
  V(I64AtomicAdd16U, Uint16)             \
  V(I64AtomicAdd32U, Uint32)             \
  V(I32AtomicSub, Uint32)                \
  V(I64AtomicSub, Uint64)                \
  V(I32AtomicSub8U, Uint8)               \
  V(I32AtomicSub16U, Uint16)             \
  V(I64AtomicSub8U, Uint8)               \
  V(I64AtomicSub16U, Uint16)             \
  V(I64AtomicSub32U, Uint32)             \
  V(I32AtomicAnd, Uint32)                \
  V(I64AtomicAnd, Uint64)                \
  V(I32AtomicAnd8U, Uint8)               \
  V(I32AtomicAnd16U, Uint16)             \
  V(I64AtomicAnd8U, Uint8)               \
  V(I64AtomicAnd16U, Uint16)             \
  V(I64AtomicAnd32U, Uint32)             \
  V(I32AtomicOr, Uint32)                 \
  V(I64AtomicOr, Uint64)                 \
  V(I32AtomicOr8U, Uint8)                \
  V(I32AtomicOr16U, Uint16)              \
  V(I64AtomicOr8U, Uint8)                \
  V(I64AtomicOr16U, Uint16)              \
  V(I64AtomicOr32U, Uint32)              \
  V(I32AtomicXor, Uint32)                \
  V(I64AtomicXor, Uint64)                \
  V(I32AtomicXor8U, Uint8)               \
  V(I32AtomicXor16U, Uint16)             \
  V(I64AtomicXor8U, Uint8)               \
  V(I64AtomicXor16U, Uint16)             \
  V(I64AtomicXor32U, Uint32)             \
  V(I32AtomicExchange, Uint32)           \
  V(I64AtomicExchange, Uint64)           \
  V(I32AtomicExchange8U, Uint8)          \
  V(I32AtomicExchange16U, Uint16)        \
  V(I64AtomicExchange8U, Uint8)          \
  V(I64AtomicExchange16U, Uint16)        \
  V(I64AtomicExchange32U, Uint32)        \
  V(I32AtomicCompareExchange, Uint32)    \
  V(I64AtomicCompareExchange, Uint64)    \
  V(I32AtomicCompareExchange8U, Uint8)   \
  V(I32AtomicCompareExchange16U, Uint16) \
  V(I64AtomicCompareExchange8U, Uint8)   \
  V(I64AtomicCompareExchange16U, Uint16) \
  V(I64AtomicCompareExchange32U, Uint32)

#define ATOMIC_STORE_OP_LIST(V) \
  V(I32AtomicStore, Uint32)     \
  V(I64AtomicStore, Uint64)     \
  V(I32AtomicStore8U, Uint8)    \
  V(I32AtomicStore16U, Uint16)  \
  V(I64AtomicStore8U, Uint8)    \
  V(I64AtomicStore16U, Uint16)  \
  V(I64AtomicStore32U, Uint32)

// Decoder error with explicit PC and optional format arguments.
// Depending on the validation tag and the number of arguments, this forwards to
// a V8_NOINLINE and V8_PRESERVE_MOST method of the decoder.
template <typename ValidationTag, typename... Args>
V8_INLINE void DecodeError(Decoder* decoder, const uint8_t* pc, const char* str,
                           Args&&... args) {
  // Decode errors can only happen if we are validating; the compiler should
  // know this e.g. from the VALIDATE macro, but this assumption tells it again
  // that this path is impossible.
  V8_ASSUME(ValidationTag::validate);
  if constexpr (!ValidationTag::full_validation) {
    decoder->MarkError();
  } else if constexpr (sizeof...(Args) == 0) {
    decoder->error(pc, str);
  } else {
    decoder->errorf(pc, str, std::forward<Args>(args)...);
  }
}

// Decoder error without explicit PC and with optional format arguments.
// Depending on the validation tag and the number of arguments, this forwards to
// a V8_NOINLINE and V8_PRESERVE_MOST method of the decoder.
template <typename ValidationTag, typename... Args>
V8_INLINE void DecodeError(Decoder* decoder, const char* str, Args&&... args) {
  // Decode errors can only happen if we are validating; the compiler should
  // know this e.g. from the VALIDATE macro, but this assumption tells it again
  // that this path is impossible.
  V8_ASSUME(ValidationTag::validate);
  if constexpr (!ValidationTag::full_validation) {
    decoder->MarkError();
  } else if constexpr (sizeof...(Args) == 0) {
    decoder->error(str);
  } else {
    decoder->errorf(str, std::forward<Args>(args)...);
  }
}

namespace value_type_reader {

template <typename ValidationTag>
std::pair<HeapType, uint32_t> read_heap_type(Decoder* decoder,
                                             const uint8_t* pc,
                                             WasmEnabledFeatures enabled) {
  auto [heap_index, length] =
      decoder->read_i33v<ValidationTag>(pc, "heap type");
  if (heap_index < 0) {
    int64_t min_1_byte_leb128 = -64;
    if (!VALIDATE(heap_index >= min_1_byte_leb128)) {
      DecodeError<ValidationTag>(decoder, pc, "Unknown heap type %" PRId64,
                                 heap_index);
      return {HeapType(HeapType::kBottom), length};
    }
    uint8_t uint_7_mask = 0x7F;
    uint8_t code = static_cast<ValueTypeCode>(heap_index) & uint_7_mask;
    bool is_shared = false;
    if (code == kSharedFlagCode) {
      if (!VALIDATE(enabled.has_shared())) {
        DecodeError<ValidationTag>(
            decoder, pc,
            "invalid heap type 0x%x, enable with --experimental-wasm-shared",
            kSharedFlagCode);
        return {HeapType(HeapType::kBottom), length};
      }
      code = decoder->read_u8<ValidationTag>(pc + length, "heap type");
      length++;
      is_shared = true;
    }
    switch (code) {
      case kEqRefCode:
      case kI31RefCode:
      case kStructRefCode:
      case kArrayRefCode:
      case kAnyRefCode:
      case kNoneCode:
      case kNoExternCode:
      case kNoFuncCode:
      case kExternRefCode:
      case kFuncRefCode:
        return {HeapType::from_code(code, is_shared), length};
      case kNoExnCode:
      case kExnRefCode:
        if (!VALIDATE(enabled.has_exnref())) {
          DecodeError<ValidationTag>(
              decoder, pc,
              "invalid heap type '%s', enable with --experimental-wasm-exnref",
              HeapType::from_code(code, is_shared).name().c_str());
        }
        return {HeapType::from_code(code, is_shared), length};
      case kStringRefCode:
      case kStringViewWtf8Code:
      case kStringViewWtf16Code:
      case kStringViewIterCode:
        if (!VALIDATE(enabled.has_stringref())) {
          DecodeError<ValidationTag>(
              decoder, pc,
              "invalid heap type '%s', enable with "
              "--experimental-wasm-stringref",
              HeapType::from_code(code, is_shared).name().c_str());
        }
        return {HeapType::from_code(code, is_shared), length};
      default:
        DecodeError<ValidationTag>(decoder, pc, "Unknown heap type %" PRId64,
                                   heap_index);
        return {HeapType(HeapType::kBottom), length};
    }
  } else {
    uint32_t type_index = static_cast<uint32_t>(heap_index);
    if (!VALIDATE(type_index < kV8MaxWasmTypes)) {
      DecodeError<ValidationTag>(
          decoder, pc,
          "Type index %u is greater than the maximum number %zu "
          "of type definitions supported by V8",
          type_index, kV8MaxWasmTypes);
      return {HeapType(HeapType::kBottom), length};
    }
    return {HeapType(type_index), length};
  }
}

// Read a value type starting at address {pc} using {decoder}.
// No bytes are consumed.
// Returns the read value type and the number of bytes read (a.k.a. length).
template <typename ValidationTag>
std::pair<ValueType, uint32_t> read_value_type(Decoder* decoder,
                                               const uint8_t* pc,
                                               WasmEnabledFeatures enabled) {
  uint8_t val = decoder->read_u8<ValidationTag>(pc, "value type opcode");
  if (!VALIDATE(decoder->ok())) {
    return {kWasmBottom, 0};
  }
  ValueTypeCode code = static_cast<ValueTypeCode>(val);
  switch (code) {
    case kEqRefCode:
    case kI31RefCode:
    case kStructRefCode:
    case kArrayRefCode:
    case kAnyRefCode:
    case kNoneCode:
    case kNoExternCode:
    case kNoFuncCode:
    case kExternRefCode:
    case kFuncRefCode:
      return {ValueType::RefNull(HeapType::from_code(code, false)), 1};
    case kNoExnCode:
    case kExnRefCode:
      if (!VALIDATE(enabled.has_exnref())) {
        DecodeError<ValidationTag>(
            decoder, pc,
            "invalid value type '%s', enable with --experimental-wasm-exnref",
            HeapType::from_code(code, false).name().c_str());
        return {kWasmBottom, 0};
      }
      return {code == kExnRefCode ? kWasmExnRef : kWasmNullExnRef, 1};
    case kStringRefCode:
    case kStringViewWtf8Code:
    case kStringViewWtf16Code:
    case kStringViewIterCode: {
      if (!VALIDATE(enabled.has_stringref())) {
        DecodeError<ValidationTag>(
            decoder, pc,
            "invalid value type '%sref', enable with "
            "--experimental-wasm-stringref",
            HeapType::from_code(code, false).name().c_str());
        return {kWasmBottom, 0};
      }
      // String views are not nullable, so interpret the shorthand accordingly.
      ValueType type = code == kStringRefCode
                           ? kWasmStringRef
                           : ValueType::Ref(HeapType::from_code(code, false));
      return {type, 1};
    }
    case kI32Code:
      return {kWasmI32, 1};
    case kI64Code:
      return {kWasmI64, 1};
    case kF32Code:
      return {kWasmF32, 1};
    case kF64Code:
      return {kWasmF64, 1};
    case kRefCode:
    case kRefNullCode: {
      Nullability nullability = code == kRefNullCode ? kNullable : kNonNullable;
      auto [heap_type, length] =
          read_heap_type<ValidationTag>(decoder, pc + 1, enabled);
      if (!VALIDATE(!heap_type.is_string_view() ||
                    nullability == kNonNullable)) {
        DecodeError<ValidationTag>(decoder, pc,
                                   "nullable string views don't exist");
        return {kWasmBottom, 0};
      }
      ValueType type = heap_type.is_bottom()
                           ? kWasmBottom
                           : ValueType::RefMaybeNull(heap_type, nullability);
      return {type, length + 1};
    }
    case kS128Code: {
      if (!VALIDATE(CheckHardwareSupportsSimd())) {
        if (v8_flags.correctness_fuzzer_suppressions) {
          FATAL("Aborting on missing Wasm SIMD support");
        }
        DecodeError<ValidationTag>(decoder, pc, "Wasm SIMD unsupported");
        return {kWasmBottom, 0};
      }
      return {kWasmS128, 1};
    }
    // Although these codes are included in ValueTypeCode, they technically
    // do not correspond to value types and are only used in specific
    // contexts. The caller of this function is responsible for handling them.
    case kVoidCode:
    case kI8Code:
    case kI16Code:
    case kF16Code:
      // Fall through to the error reporting below.
      break;
  }
  // Anything that doesn't match an enumeration value is an invalid type code.
  if constexpr (!ValidationTag::validate) UNREACHABLE();
  DecodeError<ValidationTag>(decoder, pc, "invalid value type 0x%x", code);
  return {kWasmBottom, 0};
}

template <typename ValidationTag>
bool ValidateHeapType(Decoder* decoder, const uint8_t* pc,
                      const WasmModule* module, HeapType type) {
  if (!VALIDATE(!type.is_bottom())) return false;
  if (!type.is_index()) return true;
  // A {nullptr} module is accepted if we are not validating anyway (e.g. for
  // opcode length computation).
  if (!ValidationTag::validate && module == nullptr) return true;
  if (!VALIDATE(type.ref_index() < module->types.size())) {
    DecodeError<ValidationTag>(decoder, pc, "Type index %u is out of bounds",
                               type.ref_index());
    return false;
  }
  return true;
}

template <typename ValidationTag>
bool ValidateValueType(Decoder* decoder, const uint8_t* pc,
                       const WasmModule* module, ValueType type) {
  if (!VALIDATE(!type.is_bottom())) return false;
  if (V8_LIKELY(!type.is_object_reference())) return true;
  return ValidateHeapType<ValidationTag>(decoder, pc, module, type.heap_type());
}

}  // namespace value_type_reader

enum DecodingMode { kFunctionBody, kConstantExpression };

// Helpers for decoding different kinds of immediates which follow bytecodes.
struct ImmI32Immediate {
  int32_t value;
  uint32_t length;

  template <typename ValidationTag>
  ImmI32Immediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    std::tie(value, length) = decoder->read_i32v<ValidationTag>(pc, "immi32");
  }
};

struct ImmI64Immediate {
  int64_t value;
  uint32_t length;

  template <typename ValidationTag>
  ImmI64Immediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    std::tie(value, length) = decoder->read_i64v<ValidationTag>(pc, "immi64");
  }
};

struct ImmF32Immediate {
  float value;
  uint32_t length = 4;

  template <typename ValidationTag>
  ImmF32Immediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    // We can't use base::bit_cast here because calling any helper function
    // that returns a float would potentially flip NaN bits per C++ semantics,
    // so we have to inline the memcpy call directly.
    uint32_t tmp = decoder->read_u32<ValidationTag>(pc, "immf32");
    memcpy(&value, &tmp, sizeof(value));
  }
};

struct ImmF64Immediate {
  double value;
  uint32_t length = 8;

  template <typename ValidationTag>
  ImmF64Immediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    // Avoid base::bit_cast because it might not preserve the signalling bit
    // of a NaN.
    uint64_t tmp = decoder->read_u64<ValidationTag>(pc, "immf64");
    memcpy(&value, &tmp, sizeof(value));
  }
};

struct BrOnCastFlags {
  enum Values {
    SRC_IS_NULL = 1,
    RES_IS_NULL = 1 << 1,
  };

  bool src_is_null = false;
  bool res_is_null = false;

  BrOnCastFlags() = default;
  explicit BrOnCastFlags(uint8_t value)
      : src_is_null((value & BrOnCastFlags::SRC_IS_NULL) != 0),
        res_is_null((value & BrOnCastFlags::RES_IS_NULL) != 0) {
    DCHECK_LE(value, BrOnCastFlags::SRC_IS_NULL | BrOnCastFlags::RES_IS_NULL);
  }
};

struct BrOnCastImmediate {
  BrOnCastFlags flags;
  uint8_t raw_value = 0;
  uint32_t length = 1;

  template <typename ValidationTag>
  BrOnCastImmediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    raw_value = decoder->read_u8<ValidationTag>(pc, "br_on_cast flags");
    if (raw_value > (BrOnCastFlags::SRC_IS_NULL | BrOnCastFlags::RES_IS_NULL)) {
      if constexpr (ValidationTag::full_validation) {
        decoder->errorf(pc, "invalid br_on_cast flags %u", raw_value);
      } else {
        decoder->MarkError();
      }
      return;
    }
    flags = BrOnCastFlags(raw_value);
  }
};

// Parent class for all Immediates which read a u32v index value in their
// constructor.
struct IndexImmediate {
  uint32_t index;
  uint32_t length;

  template <typename ValidationTag>
  IndexImmediate(Decoder* decoder, const uint8_t* pc, const char* name,
                 ValidationTag = {}) {
    std::tie(index, length) = decoder->read_u32v<ValidationTag>(pc, name);
  }
};

struct MemoryIndexImmediate : public IndexImmediate {
  const WasmMemory* memory = nullptr;

  template <typename ValidationTag>
  MemoryIndexImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "memory index", validate) {}
};

struct TableIndexImmediate : public IndexImmediate {
  const WasmTable* table = nullptr;

  template <typename ValidationTag>
  TableIndexImmediate(Decoder* decoder, const uint8_t* pc,
                      ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "table index", validate) {}
};

struct TagIndexImmediate : public IndexImmediate {
  const WasmTag* tag = nullptr;

  template <typename ValidationTag>
  TagIndexImmediate(Decoder* decoder, const uint8_t* pc,
                    ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "tag index", validate) {}
};

struct GlobalIndexImmediate : public IndexImmediate {
  const WasmGlobal* global = nullptr;

  template <typename ValidationTag>
  GlobalIndexImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "global index", validate) {}
};

struct SigIndexImmediate : public IndexImmediate {
  const FunctionSig* sig = nullptr;

  template <typename ValidationTag>
  SigIndexImmediate(Decoder* decoder, const uint8_t* pc,
                    ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "signature index", validate) {}
};

struct StructIndexImmediate : public IndexImmediate {
  const StructType* struct_type = nullptr;

  template <typename ValidationTag>
  StructIndexImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "struct index", validate) {}
};

struct ArrayIndexImmediate : public IndexImmediate {
  const ArrayType* array_type = nullptr;

  template <typename ValidationTag>
  ArrayIndexImmediate(Decoder* decoder, const uint8_t* pc,
                      ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "array index", validate) {}
};

struct CallFunctionImmediate : public IndexImmediate {
  const FunctionSig* sig = nullptr;

  template <typename ValidationTag>
  CallFunctionImmediate(Decoder* decoder, const uint8_t* pc,
                        ValidationTag validate = {})
      : IndexImmediate(decoder, pc, "function index", validate) {}
};

struct SelectTypeImmediate {
  uint32_t length;
  ValueType type;

  template <typename ValidationTag>
  SelectTypeImmediate(WasmEnabledFeatures enabled, Decoder* decoder,
                      const uint8_t* pc, ValidationTag = {}) {
    uint8_t num_types;
    std::tie(num_types, length) =
        decoder->read_u32v<ValidationTag>(pc, "number of select types");
    if (!VALIDATE(num_types == 1)) {
      DecodeError<ValidationTag>(
          decoder, pc,
          "Invalid number of types. Select accepts exactly one type");
      return;
    }
    uint32_t type_length;
    std::tie(type, type_length) =
        value_type_reader::read_value_type<ValidationTag>(decoder, pc + length,
                                                          enabled);
    length += type_length;
  }
};

static constexpr uint32_t kInlineSignatureSentinel = UINT_MAX;

struct BlockTypeImmediate {
  uint32_t length = 1;
  // After decoding, either {sig_index} is set XOR {sig} points to
  // {single_return_sig_storage}.
  uint32_t sig_index = kInlineSignatureSentinel;
  FunctionSig sig{0, 0, single_return_sig_storage};
  // Internal field, potentially pointed to by {sig}. Do not access directly.
  ValueType single_return_sig_storage[1];

  // Do not copy or move, as {sig} might point to {single_return_sig_storage} so
  // this cannot trivially be copied. If needed, define those operators later.
  BlockTypeImmediate(const BlockTypeImmediate&) = delete;
  BlockTypeImmediate(BlockTypeImmediate&&) = delete;
  BlockTypeImmediate& operator=(const BlockTypeImmediate&) = delete;
  BlockTypeImmediate& operator=(BlockTypeImmediate&&) = delete;

  template <typename ValidationTag>
  BlockTypeImmediate(WasmEnabledFeatures enabled, Decoder* decoder,
                     const uint8_t* pc, ValidationTag = {}) {
    int64_t block_type;
    std::tie(block_type, length) =
        decoder->read_i33v<ValidationTag>(pc, "block type");
    if (block_type < 0) {
      // All valid negative types are 1 byte in length, so we check against the
      // minimum 1-byte LEB128 value.
      constexpr int64_t min_1_byte_leb128 = -64;
      if (!VALIDATE(block_type >= min_1_byte_leb128)) {
        DecodeError<ValidationTag>(decoder, pc, "invalid block type %" PRId64,
                                   block_type);
        return;
      }
      if (static_cast<ValueTypeCode>(block_type & 0x7F) != kVoidCode) {
        sig = FunctionSig{1, 0, single_return_sig_storage};
        std::tie(single_return_sig_storage[0], length) =
            value_type_reader::read_value_type<ValidationTag>(decoder, pc,
                                                              enabled);
      }
    } else {
      sig = FunctionSig{0, 0, nullptr};
      sig_index = static_cast<uint32_t>(block_type);
    }
  }

  uint32_t in_arity() const {
    return static_cast<uint32_t>(sig.parameter_count());
  }
  uint32_t out_arity() const {
    return static_cast<uint32_t>(sig.return_count());
  }
  ValueType in_type(uint32_t index) const { return sig.GetParam(index); }
  ValueType out_type(uint32_t index) const { return sig.GetReturn(index); }
};

struct BranchDepthImmediate {
  uint32_t depth;
  uint32_t length;

  template <typename ValidationTag>
  BranchDepthImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag = {}) {
    std::tie(depth, length) =
        decoder->read_u32v<ValidationTag>(pc, "branch depth");
  }
};

struct FieldImmediate {
  StructIndexImmediate struct_imm;
  IndexImmediate field_imm;
  uint32_t length;

  template <typename ValidationTag>
  FieldImmediate(Decoder* decoder, const uint8_t* pc,
                 ValidationTag validate = {})
      : struct_imm(decoder, pc, validate),
        field_imm(decoder, pc + struct_imm.length, "field index", validate),
        length(struct_imm.length + field_imm.length) {}
};

struct CallIndirectImmediate {
  IndexImmediate sig_imm;
  TableIndexImmediate table_imm;
  uint32_t length;
  const FunctionSig* sig = nullptr;

  template <typename ValidationTag>
  CallIndirectImmediate(Decoder* decoder, const uint8_t* pc,
                        ValidationTag validate = {})
      : sig_imm(decoder, pc, "singature index", validate),
        table_imm(decoder, pc + sig_imm.length, validate),
        length(sig_imm.length + table_imm.length) {}
};

struct BranchTableImmediate {
  uint32_t table_count;
  const uint8_t* start;
  const uint8_t* table;

  template <typename ValidationTag>
  BranchTableImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag = {}) {
    start = pc;
    uint32_t len;
    std::tie(table_count, len) =
        decoder->read_u32v<ValidationTag>(pc, "table count");
    table = pc + len;
  }
};

using TryTableImmediate = BranchTableImmediate;

// A helper to iterate over a branch table.
template <typename ValidationTag>
class BranchTableIterator {
 public:
  uint32_t cur_index() const { return index_; }
  bool has_next() const {
    return VALIDATE(decoder_->ok()) && index_ <= table_count_;
  }
  uint32_t next() {
    DCHECK(has_next());
    index_++;
    auto [result, length] =
        decoder_->read_u32v<ValidationTag>(pc_, "branch table entry");
    pc_ += length;
    return result;
  }
  // length, including the length of the {BranchTableImmediate}, but not the
  // opcode. This consumes the table entries, so it is invalid to call next()
  // before or after this method.
  uint32_t length() {
    while (has_next()) next();
    return static_cast<uint32_t>(pc_ - start_);
  }
  const uint8_t* pc() const { return pc_; }

  BranchTableIterator(Decoder* decoder, const BranchTableImmediate& imm)
      : decoder_(decoder),
        start_(imm.start),
        pc_(imm.table),
        table_count_(imm.table_count) {}

 private:
  Decoder* const decoder_;
  const uint8_t* const start_;
  const uint8_t* pc_;
  uint32_t index_ = 0;          // the current index.
  const uint32_t table_count_;  // the count of entries, not including default.
};

struct CatchCase {
  CatchKind kind;
  // The union contains a TagIndexImmediate iff kind == kCatch or kind ==
  // kCatchRef.
  union MaybeTagIndex {
    uint8_t empty;
    TagIndexImmediate tag_imm;
  } maybe_tag;
  BranchDepthImmediate br_imm;
};

// A helper to iterate over a try table.
template <typename ValidationTag>
class TryTableIterator {
 public:
  uint32_t cur_index() const { return index_; }
  bool has_next() const {
    return VALIDATE(decoder_->ok()) && index_ < table_count_;
  }

  CatchCase next() {
    uint8_t kind =
        static_cast<CatchKind>(decoder_->read_u8<ValidationTag>(pc_));
    pc_ += 1;
    CatchCase::MaybeTagIndex maybe_tag{0};
    if (kind == kCatch || kind == kCatchRef) {
      maybe_tag.tag_imm = TagIndexImmediate(decoder_, pc_, ValidationTag{});
      pc_ += maybe_tag.tag_imm.length;
    }
    BranchDepthImmediate br_imm(decoder_, pc_, ValidationTag{});
    pc_ += br_imm.length;
    index_++;
    return CatchCase{static_cast<CatchKind>(kind), maybe_tag, br_imm};
  }

  // length, including the length of the {TryTableImmediate}, but not the
  // opcode. This consumes the table entries, so it is invalid to call next()
  // before or after this method.
  uint32_t length() {
    while (has_next()) next();
    return static_cast<uint32_t>(pc_ - start_);
  }
  const uint8_t* pc() const { return pc_; }

  TryTableIterator(Decoder* decoder, const TryTableImmediate& imm)
      : decoder_(decoder),
        start_(imm.start),
        pc_(imm.table),
        table_count_(imm.table_count) {}

 private:
  Decoder* const decoder_;
  const uint8_t* const start_;
  const uint8_t* pc_;
  uint32_t index_ = 0;          // the current index.
  const uint32_t table_count_;  // the count of entries, not including default.
};

struct MemoryAccessImmediate {
  uint32_t alignment;
  uint32_t mem_index;
  uint64_t offset;
  const WasmMemory* memory = nullptr;

  uint32_t length;

  template <typename ValidationTag>
  V8_INLINE MemoryAccessImmediate(Decoder* decoder, const uint8_t* pc,
                                  uint32_t max_alignment, bool memory64_enabled,
                                  ValidationTag = {}) {
    // Check for the fast path (two single-byte LEBs, mem index 0).
    const bool two_bytes = !ValidationTag::validate || decoder->end() - pc >= 2;
    const bool use_fast_path = two_bytes && !(pc[0] & 0xc0) && !(pc[1] & 0x80);
    if (V8_LIKELY(use_fast_path)) {
      alignment = pc[0];
      mem_index = 0;
      offset = pc[1];
      length = 2;
    } else {
      ConstructSlow<ValidationTag>(decoder, pc, max_alignment,
                                   memory64_enabled);
    }
    if (!VALIDATE(alignment <= max_alignment)) {
      DecodeError<ValidationTag>(
          decoder, pc,
          "invalid alignment; expected maximum alignment is %u, "
          "actual alignment is %u",
          max_alignment, alignment);
    }
  }

 private:
  template <typename ValidationTag>
  V8_NOINLINE V8_PRESERVE_MOST void ConstructSlow(Decoder* decoder,
                                                  const uint8_t* pc,
                                                  uint32_t max_alignment,
                                                  bool memory64_enabled) {
    uint32_t alignment_length;
    std::tie(alignment, alignment_length) =
        decoder->read_u32v<ValidationTag>(pc, "alignment");
    length = alignment_length;
    if (alignment & 0x40) {
      alignment &= ~0x40;
      uint32_t mem_index_length;
      std::tie(mem_index, mem_index_length) =
          decoder->read_u32v<ValidationTag>(pc + length, "memory index");
      length += mem_index_length;
    } else {
      mem_index = 0;
    }
    uint32_t offset_length;
    if (memory64_enabled) {
      std::tie(offset, offset_length) =
          decoder->read_u64v<ValidationTag>(pc + length, "offset");
    } else {
      std::tie(offset, offset_length) =
          decoder->read_u32v<ValidationTag>(pc + length, "offset");
    }
    length += offset_length;
  }
};

// Immediate for SIMD lane operations.
struct SimdLaneImmediate {
  uint8_t lane;
  uint32_t length = 1;

  template <typename ValidationTag>
  SimdLaneImmediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    lane = decoder->read_u8<ValidationTag>(pc, "lane");
  }
};

// Immediate for SIMD S8x16 shuffle operations.
struct Simd128Immediate {
  uint8_t value[kSimd128Size] = {0};

  template <typename ValidationTag>
  Simd128Immediate(Decoder* decoder, const uint8_t* pc, ValidationTag = {}) {
    for (uint32_t i = 0; i < kSimd128Size; ++i) {
      value[i] = decoder->read_u8<ValidationTag>(pc + i, "value");
    }
  }
};

struct MemoryInitImmediate {
  IndexImmediate data_segment;
  MemoryIndexImmediate memory;
  uint32_t length;

  template <typename ValidationTag>
  MemoryInitImmediate(Decoder* decoder, const uint8_t* pc,
                      ValidationTag validate = {})
      : data_segment(decoder, pc, "data segment index", validate),
        memory(decoder, pc + data_segment.length, validate),
        length(data_segment.length + memory.length) {}
};

struct MemoryCopyImmediate {
  MemoryIndexImmediate memory_dst;
  MemoryIndexImmediate memory_src;
  uint32_t length;

  template <typename ValidationTag>
  MemoryCopyImmediate(Decoder* decoder, const uint8_t* pc,
                      ValidationTag validate = {})
      : memory_dst(decoder, pc, validate),
        memory_src(decoder, pc + memory_dst.length, validate),
        length(memory_src.length + memory_dst.length) {}
};

struct TableInitImmediate {
  IndexImmediate element_segment;
  TableIndexImmediate table;
  uint32_t length;

  template <typename ValidationTag>
  TableInitImmediate(Decoder* decoder, const uint8_t* pc,
                     ValidationTag validate = {})
      : element_segment(decoder, pc, "element segment index", validate),
        table(decoder, pc + element_segment.length, validate),
        length(element_segment.length + table.length) {}
};

struct TableCopyImmediate {
  TableIndexImmediate table_dst;
  TableIndexImmediate table_src;
  uint32_t length;

  template <typename ValidationTag>
  TableCopyImmediate(Decoder* decoder, const uint8_t* pc,
                     ValidationTag validate = {})
      : table_dst(decoder, pc, validate),
        table_src(decoder, pc + table_dst.length, validate),
        length(table_src.length + table_dst.length) {}
};

struct HeapTypeImmediate {
  uint32_t length;
  HeapType type{kBottom};

  template <typename ValidationTag>
  HeapTypeImmediate(WasmEnabledFeatures enabled, Decoder* decoder,
                    const uint8_t* pc, ValidationTag = {}) {
    std::tie(type, length) =
        value_type_reader::read_heap_type<ValidationTag>(decoder, pc, enabled);
  }
};

struct StringConstImmediate {
  uint32_t index;
  uint32_t length;

  template <typename ValidationTag>
  StringConstImmediate(Decoder* decoder, const uint8_t* pc,
                       ValidationTag = {}) {
    std::tie(index, length) =
        decoder->read_u32v<ValidationTag>(pc, "stringref literal index");
  }
};

template <bool full_validation>
struct PcForErrors {
  static_assert(full_validation == false);
  explicit PcForErrors(const uint8_t* /* pc */) {}

  const uint8_t* pc() const { return nullptr; }
};

template <>
struct PcForErrors<true> {
  const uint8_t* pc_for_errors = nullptr;

  explicit PcForErrors(const uint8_t* pc) : pc_for_errors(pc) {}

  const uint8_t* pc() const { return pc_for_errors; }
};

// An entry on the value stack.
template <typename ValidationTag>
struct ValueBase : public PcForErrors<ValidationTag::full_validation> {
  ValueType type = kWasmVoid;

  ValueBase(const uint8_t* pc, ValueType type)
      : PcForErrors<ValidationTag::full_validation>(pc), type(type) {}
};

template <typename Value>
struct Merge {
  uint32_t arity = 0;
  union {  // Either multiple values or a single value.
    Value* array;
    Value first;
  } vals = {nullptr};  // Initialize {array} with {nullptr}.

  // Tracks whether this merge was ever reached. Uses precise reachability, like
  // Reachability::kReachable.
  bool reached;

  explicit Merge(bool reached = false) : reached(reached) {}

  Value& operator[](uint32_t i) {
    DCHECK_GT(arity, i);
    return arity == 1 ? vals.first : vals.array[i];
  }
};

enum ControlKind : uint8_t {
  kControlIf,
  kControlIfElse,
  kControlBlock,
  kControlLoop,
  kControlTry,
  kControlTryTable,
  kControlTryCatch,
  kControlTryCatchAll,
};

enum Reachability : uint8_t {
  // reachable code.
  kReachable,
  // reachable code in unreachable block (implies normal validation).
  kSpecOnlyReachable,
  // code unreachable in its own block (implies polymorphic validation).
  kUnreachable
};

// An entry on the control stack (i.e. if, block, loop, or try).
template <typename Value, typename ValidationTag>
struct ControlBase : public PcForErrors<ValidationTag::full_validation> {
  ControlKind kind = kControlBlock;
  Reachability reachability = kReachable;

  // For try-table.
  base::Vector<CatchCase> catch_cases;

  uint32_t stack_depth = 0;  // Stack height at the beginning of the construct.
  uint32_t init_stack_depth = 0;  // Height of "locals initialization" stack
                                  // at the beginning of the construct.
  int32_t previous_catch = -1;  // Depth of the innermost catch containing this
                                // 'try'.

  // Values merged into the start or end of this control construct.
  Merge<Value> start_merge;
  Merge<Value> end_merge;

  bool might_throw = false;

  MOVE_ONLY_NO_DEFAULT_CONSTRUCTOR(ControlBase);

  ControlBase(Zone* zone, ControlKind kind, uint32_t stack_depth,
              uint32_t init_stack_depth, const uint8_t* pc,
              Reachability reachability)
      : PcForErrors<ValidationTag::full_validation>(pc),
        kind(kind),
        reachability(reachability),
        stack_depth(stack_depth),
        init_stack_depth(init_stack_depth),
        start_merge(reachability == kReachable) {}

  // Check whether the current block is reachable.
  bool reachable() const { return reachability == kReachable; }

  // Check whether the rest of the block is unreachable.
  // Note that this is different from {!reachable()}, as there is also the
  // "indirect unreachable state", for which both {reachable()} and
  // {unreachable()} return false.
  bool unreachable() const { return reachability == kUnreachable; }

  // Return the reachability of new control structs started in this block.
  Reachability innerReachability() const {
    return reachability == kReachable ? kReachable : kSpecOnlyReachable;
  }

  bool is_if() const { return is_onearmed_if() || is_if_else(); }
  bool is_onearmed_if() const { return kind == kControlIf; }
  bool is_if_else() const { return kind == kControlIfElse; }
  bool is_block() const { return kind == kControlBlock; }
  bool is_loop() const { return kind == kControlLoop; }
  bool is_incomplete_try() const { return kind == kControlTry; }
  bool is_try_catch() const { return kind == kControlTryCatch; }
  bool is_try_catchall() const { return kind == kControlTryCatchAll; }
  bool is_try() const {
    return is_incomplete_try() || is_try_catch() || is_try_catchall();
  }
  bool is_try_table() { return kind == kControlTryTable; }

  Merge<Value>* br_merge() {
    return is_loop() ? &this->start_merge : &this->end_merge;
  }
};

// This is the list of callback functions that an interface for the
// WasmFullDecoder should implement.
// F(Name, args...)
#define INTERFACE_FUNCTIONS(F)    \
  INTERFACE_META_FUNCTIONS(F)     \
  INTERFACE_CONSTANT_FUNCTIONS(F) \
  INTERFACE_NON_CONSTANT_FUNCTIONS(F)

#define INTERFACE_META_FUNCTIONS(F)    \
  F(TraceInstruction, uint32_t value)  \
  F(StartFunction)                     \
  F(StartFunctionBody, Control* block) \
  F(FinishFunction)                    \
  F(OnFirstError)                      \
  F(NextInstruction, WasmOpcode)

#define INTERFACE_CONSTANT_FUNCTIONS(F) /*       force 80 columns           */ \
  F(I32Const, Value* result, int32_t value)                                    \
  F(I64Const, Value* result, int64_t value)                                    \
  F(F32Const, Value* result, float value)                                      \
  F(F64Const, Value* result, double value)                                     \
  F(S128Const, const Simd128Immediate& imm, Value* result)                     \
  F(GlobalGet, Value* result, const GlobalIndexImmediate& imm)                 \
  F(DoReturn, uint32_t drop_values)                                            \
  F(UnOp, WasmOpcode opcode, const Value& value, Value* result)                \
  F(BinOp, WasmOpcode opcode, const Value& lhs, const Value& rhs,              \
    Value* result)                                                             \
  F(RefNull, ValueType type, Value* result)                                    \
  F(RefFunc, uint32_t function_index, Value* result)                           \
  F(StructNew, const StructIndexImmediate& imm, const Value args[],            \
    Value* result)                                                             \
  F(StructNewDefault, const StructIndexImmediate& imm, Value* result)          \
  F(ArrayNew, const ArrayIndexImmediate& imm, const Value& length,             \
    const Value& initial_value, Value* result)                                 \
  F(ArrayNewDefault, const ArrayIndexImmediate& imm, const Value& length,      \
    Value* result)                                                             \
  F(ArrayNewFixed, const ArrayIndexImmediate& imm,                             \
    const IndexImmediate& length_imm, const Value elements[], Value* result)   \
  F(ArrayNewSegment, const ArrayIndexImmediate& array_imm,                     \
    const IndexImmediate& data_segment, const Value& offset,                   \
    const Value& length, Value* result)                                        \
  F(RefI31, const Value& input, Value* result)                                 \
  F(StringConst, const StringConstImmediate& imm, Value* result)

#define INTERFACE_NON_CONSTANT_FUNCTIONS(F) /*       force 80 columns       */ \
  /* Control: */                                                               \
  F(Block, Control* block)                                                     \
  F(Loop, Control* block)                                                      \
  F(Try, Control* block)                                                       \
  F(TryTable, Control* block)                                                  \
  F(CatchCase, Control* block, const CatchCase& catch_case,                    \
    base::Vector<Value> caught_values)                                         \
  F(If, const Value& cond, Control* if_block)                                  \
  F(FallThruTo, Control* c)                                                    \
  F(PopControl, Control* block)                                                \
  /* Instructions: */                                                          \
  F(RefAsNonNull, const Value& arg, Value* result)                             \
  F(Drop)                                                                      \
  F(LocalGet, Value* result, const IndexImmediate& imm)                        \
  F(LocalSet, const Value& value, const IndexImmediate& imm)                   \
  F(LocalTee, const Value& value, Value* result, const IndexImmediate& imm)    \
  F(GlobalSet, const Value& value, const GlobalIndexImmediate& imm)            \
  F(TableGet, const Value& index, Value* result, const IndexImmediate& imm)    \
  F(TableSet, const Value& index, const Value& value,                          \
    const IndexImmediate& imm)                                                 \
  F(Trap, TrapReason reason)                                                   \
  F(NopForTestingUnsupportedInLiftoff)                                         \
  F(Forward, const Value& from, Value* to)                                     \
  F(Select, const Value& cond, const Value& fval, const Value& tval,           \
    Value* result)                                                             \
  F(BrOrRet, uint32_t depth)                                                   \
  F(BrIf, const Value& cond, uint32_t depth)                                   \
  F(BrTable, const BranchTableImmediate& imm, const Value& key)                \
  F(Else, Control* if_block)                                                   \
  F(LoadMem, LoadType type, const MemoryAccessImmediate& imm,                  \
    const Value& index, Value* result)                                         \
  F(LoadTransform, LoadType type, LoadTransformationKind transform,            \
    const MemoryAccessImmediate& imm, const Value& index, Value* result)       \
  F(LoadLane, LoadType type, const Value& value, const Value& index,           \
    const MemoryAccessImmediate& imm, const uint8_t laneidx, Value* result)    \
  F(StoreMem, StoreType type, const MemoryAccessImmediate& imm,                \
    const Value& index, const Value& value)                                    \
  F(StoreLane, StoreType type, const MemoryAccessImmediate& imm,               \
    const Value& index, const Value& value, const uint8_t laneidx)             \
  F(CurrentMemoryPages, const MemoryIndexImmediate& imm, Value* result)        \
  F(MemoryGrow, const MemoryIndexImmediate& imm, const Value& value,           \
    Value* result)                                                             \
  F(CallDirect, const CallFunctionImmediate& imm, const Value args[],          \
    Value returns[])                                                           \
  F(CallIndirect, const Value& index, const CallIndirectImmediate& imm,        \
    const Value args[], Value returns[])                                       \
  F(CallRef, const Value& func_ref, const FunctionSig* sig,                    \
    const Value args[], const Value returns[])                                 \
  F(ReturnCallRef, const Value& func_ref, const FunctionSig* sig,              \
    const Value args[])                                                        \
  F(ReturnCall, const CallFunctionImmediate& imm, const Value args[])          \
  F(ReturnCallIndirect, const Value& index, const CallIndirectImmediate& imm,  \
    const Value args[])                                                        \
  F(BrOnNull, const Value& ref_object, uint32_t depth,                         \
    bool pass_null_along_branch, Value* result_on_fallthrough)                 \
  F(BrOnNonNull, const Value& ref_object, Value* result, uint32_t depth,       \
    bool drop_null_on_fallthrough)                                             \
  F(SimdOp, WasmOpcode opcode, const Value args[], Value* result)              \
  F(SimdLaneOp, WasmOpcode opcode, const SimdLaneImmediate& imm,               \
    base::Vector<const Value> inputs, Value* result)                           \
  F(Simd8x16ShuffleOp, const Simd128Immediate& imm, const Value& input0,       \
    const Value& input1, Value* result)                                        \
  F(Throw, const TagIndexImmediate& imm, const Value args[])                   \
  F(ThrowRef, Value* value)                                                    \
  F(Rethrow, Control* block)                                                   \
  F(CatchException, const TagIndexImmediate& imm, Control* block,              \
    base::Vector<Value> caught_values)                                         \
  F(Delegate, uint32_t depth, Control* block)                                  \
  F(CatchAll, Control* block)                                                  \
  F(AtomicOp, WasmOpcode opcode, const Value args[], const size_t argc,        \
    const MemoryAccessImmediate& imm, Value* result)                           \
  F(AtomicFence)                                                               \
  F(MemoryInit, const MemoryInitImmediate& imm, const Value& dst,              \
    const Value& src, const Value& size)                                       \
  F(DataDrop, const IndexImmediate& imm)                                       \
  F(MemoryCopy, const MemoryCopyImmediate& imm, const Value& dst,              \
    const Value& src, const Value& size)                                       \
  F(MemoryFill, const MemoryIndexImmediate& imm, const Value& dst,             \
    const Value& value, const Value& size)                                     \
  F(TableInit, const TableInitImmediate& imm, const Value& dst,                \
    const Value& src, const Value& size)                                       \
  F(ElemDrop, const IndexImmediate& imm)                                       \
  F(TableCopy, const TableCopyImmediate& imm, const Value& dst,                \
    const Value& src, const Value& size)                                       \
  F(TableGrow, const IndexImmediate& imm, const Value& value,                  \
    const Value& delta, Value* result)                                         \
  F(TableSize, const IndexImmediate& imm, Value* result)                       \
  F(TableFill, const IndexImmediate& imm, const Value& start,                  \
    const Value& value, const Value& count)                                    \
  F(StructGet, const Value& struct_object, const FieldImmediate& field,        \
    bool is_signed, Value* result)                                             \
  F(StructSet, const Value& struct_object, const FieldImmediate& field,        \
    const Value& field_value)                                                  \
  F(ArrayGet, const Value& array_obj, const ArrayIndexImmediate& imm,          \
    const Value& index, bool is_signed, Value* result)                         \
  F(ArraySet, const Value& array_obj, const ArrayIndexImmediate& imm,          \
    const Value& index, const Value& value)                                    \
  F(ArrayLen, const Value& array_obj, Value* result)                           \
  F(ArrayCopy, const Value& dst, const Value& dst_index, const Value& src,     \
    const Value& src_index, const ArrayIndexImmediate& src_imm,                \
    const Value& length)                                                       \
  F(ArrayFill, const ArrayIndexImmediate& imm, const Value& array,             \
    const Value& index, const Value& value, const Value& length)               \
  F(ArrayInitSegment, const ArrayIndexImmediate& array_imm,                    \
    const IndexImmediate& segment_imm, const Value& array,                     \
    const Value& array_index, const Value& segment_offset,                     \
    const Value& length)                                                       \
  F(I31GetS, const Value& input, Value* result)                                \
  F(I31GetU, const Value& input, Value* result)                                \
  F(RefTest, uint32_t ref_index, const Value& obj, Value* result,              \
    bool null_succeeds)                                                        \
  F(RefTestAbstract, const Value& obj, HeapType type, Value* result,           \
    bool null_succeeds)                                                        \
  F(RefCast, uint32_t ref_index, const Value& obj, Value* result,              \
    bool null_succeeds)                                                        \
  F(RefCastAbstract, const Value& obj, HeapType type, Value* result,           \
    bool null_succeeds)                                                        \
  F(AssertNullTypecheck, const Value& obj, Value* result)                      \
  F(AssertNotNullTypecheck, const Value& obj, Value* result)                   \
  F(BrOnCast, uint32_t ref_index, const Value& obj, Value* result_on_branch,   \
    uint32_t depth, bool null_succeeds)                                        \
  F(BrOnCastFail, uint32_t ref_index, const Value& obj,                        \
    Value* result_on_fallthrough, uint32_t depth, bool null_succeeds)          \
  F(BrOnCastAbstract, const Value& obj, HeapType type,                         \
    Value* result_on_branch, uint32_t depth, bool null_succeeds)               \
  F(BrOnCastFailAbstract, const Value& obj, HeapType type,                     \
    Value* result_on_fallthrough, uint32_t depth, bool null_succeeds)          \
  F(StringNewWtf8, const MemoryIndexImmediate& memory,                         \
    const unibrow::Utf8Variant variant, const Value& offset,                   \
    const Value& size, Value* result)                                          \
  F(StringNewWtf8Array, const unibrow::Utf8Variant variant,                    \
    const Value& array, const Value& start, const Value& end, Value* result)   \
  F(StringNewWtf16, const MemoryIndexImmediate& memory, const Value& offset,   \
    const Value& size, Value* result)                                          \
  F(StringNewWtf16Array, const Value& array, const Value& start,               \
    const Value& end, Value* result)                                           \
  F(StringMeasureWtf8, const unibrow::Utf8Variant variant, const Value& str,   \
    Value* result)                                                             \
  F(StringMeasureWtf16, const Value& str, Value* result)                       \
  F(StringEncodeWtf8, const MemoryIndexImmediate& memory,                      \
    const unibrow::Utf8Variant variant, const Value& str,                      \
    const Value& address, Value* result)                                       \
  F(StringEncodeWtf8Array, const unibrow::Utf8Variant variant,                 \
    const Value& str, const Value& array, const Value& start, Value* result)   \
  F(StringEncodeWtf16, const MemoryIndexImmediate& memory, const Value& str,   \
    const Value& address, Value* result)                                       \
  F(StringEncodeWtf16Array, const Value& str, const Value& array,              \
    const Value& start, Value* result)                                         \
  F(StringConcat, const Value& head, const Value& tail, Value* result)         \
  F(StringEq, const Value& a, const Value& b, Value* result)                   \
  F(StringIsUSVSequence, const Value& str, Value* result)                      \
  F(StringAsWtf8, const Value& str, Value* result)                             \
  F(StringViewWtf8Advance, const Value& view, const Value& pos,                \
    const Value& bytes, Value* result)                                         \
  F(StringViewWtf8Encode, const MemoryIndexImmediate& memory,                  \
    const unibrow::Utf8Variant variant, const Value& view, const Value& addr,  \
    const Value& pos, const Value& bytes, Value* next_pos,                     \
    Value* bytes_written)                                                      \
  F(StringViewWtf8Slice, const Value& view, const Value& start,                \
    const Value& end, Value* result)                                           \
  F(StringAsWtf16, const Value& str, Value* result)                            \
  F(StringViewWtf16GetCodeUnit, const Value& view, const Value& pos,           \
    Value* result)                                                             \
  F(StringViewWtf16Encode, const MemoryIndexImmediate& memory,                 \
    const Value& view, const Value& addr, const Value& pos,                    \
    const Value& codeunits, Value* result)                                     \
  F(StringViewWtf16Slice, const Value& view, const Value& start,               \
    const Value& end, Value* result)                                           \
  F(StringAsIter, const Value& str, Value* result)                             \
  F(StringViewIterNext, const Value& view, Value* result)                      \
  F(StringViewIterAdvance, const Value& view, const Value& codepoints,         \
    Value* result)                                                             \
  F(StringViewIterRewind, const Value& view, const Value& codepoints,          \
    Value* result)                                                             \
  F(StringViewIterSlice, const Value& view, const Value& codepoints,           \
    Value* result)                                                             \
  F(StringCompare, const Value& lhs, const Value& rhs, Value* result)          \
  F(StringFromCodePoint, const Value& code_point, Value* result)               \
  F(StringHash, const Value& string, Value* result)

// This is a global constant invalid instruction trace, to be pointed at by
// the current instruction trace pointer in the default case
const std::pair<uint32_t, uint32_t> invalid_instruction_trace = {0, 0};

// A fast vector implementation, without implicit bounds checks (see
// https://crbug.com/1358853).
template <typename T>
class FastZoneVector {
 public:
  FastZoneVector() = default;
  explicit FastZoneVector(int initial_size, Zone* zone) {
    Grow(initial_size, zone);
  }

#ifdef DEBUG
  ~FastZoneVector() {
    // Check that {Reset} was called on this vector.
    DCHECK_NULL(begin_);
  }
#endif

  void Reset(Zone* zone) {
    if (begin_ == nullptr) return;
    if constexpr (!std::is_trivially_destructible_v<T>) {
      for (T* ptr = begin_; ptr != end_; ++ptr) {
        ptr->~T();
      }
    }
    zone->DeleteArray(begin_, capacity_end_ - begin_);
    begin_ = nullptr;
    end_ = nullptr;
    capacity_end_ = nullptr;
  }

  T* begin() const { return begin_; }
  T* end() const { return end_; }

  T& front() {
    DCHECK(!empty());
    return begin_[0];
  }

  T& back() {
    DCHECK(!empty());
    return end_[-1];
  }

  uint32_t size() const { return static_cast<uint32_t>(end_ - begin_); }

  bool empty() const { return begin_ == end_; }

  T& operator[](uint32_t index) {
    DCHECK_GE(size(), index);
    return begin_[index];
  }

  void shrink_to(uint32_t new_size) {
    static_assert(std::is_trivially_destructible_v<T>);
    DCHECK_GE(size(), new_size);
    end_ = begin_ + new_size;
  }

  void pop(uint32_t num = 1) {
    DCHECK_GE(size(), num);
    for (T* new_end = end_ - num; end_ != new_end;) {
      --end_;
      end_->~T();
    }
  }

  void push(T value) {
    DCHECK_GT(capacity_end_, end_);
    *end_ = std::move(value);
    ++end_;
  }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    DCHECK_GT(capacity_end_, end_);
    new (end_) T{std::forward<Args>(args)...};
    ++end_;
  }

  V8_INLINE void EnsureMoreCapacity(int slots_needed, Zone* zone) {
    if (V8_LIKELY(capacity_end_ - end_ >= slots_needed)) return;
    Grow(slots_needed, zone);
  }

 private:
  V8_NOINLINE V8_PRESERVE_MOST void Grow(int slots_needed, Zone* zone) {
    size_t new_capacity = std::max(
        size_t{8}, base::bits::RoundUpToPowerOfTwo(size() + slots_needed));
    CHECK_GE(kMaxUInt32, new_capacity);
    DCHECK_LT(capacity_end_ - begin_, new_capacity);
    T* new_begin = zone->template AllocateArray<T>(new_capacity);
    if (begin_) {
      for (T *ptr = begin_, *new_ptr = new_begin; ptr != end_;
           ++ptr, ++new_ptr) {
        new (new_ptr) T{std::move(*ptr)};
        ptr->~T();
      }
      zone->DeleteArray(begin_, capacity_end_ - begin_);
    }
    end_ = new_begin + (end_ - begin_);
    begin_ = new_begin;
    capacity_end_ = new_begin + new_capacity;
  }

  // The array is zone-allocated inside {EnsureMoreCapacity}.
  T* begin_ = nullptr;
  T* end_ = nullptr;
  T* capacity_end_ = nullptr;
};

// Generic Wasm bytecode decoder with utilities for decoding immediates,
// lengths, etc.
template <typename ValidationTag, DecodingMode decoding_mode = kFunctionBody>
class WasmDecoder : public Decoder {
  // {full_validation} implies {validate}.
  static_assert(!ValidationTag::full_validation || ValidationTag::validate);

 public:
  WasmDecoder(Zone* zone, const WasmModule* module, WasmEnabledFeatures enabled,
              WasmDetectedFeatures* detected, const FunctionSig* sig,
              bool is_shared, const uint8_t* start, const uint8_t* end,
              uint32_t buffer_offset = 0)
      : Decoder(start, end, buffer_offset),
        zone_(zone),
        module_(module),
        enabled_(enabled),
        detected_(detected),
        sig_(sig),
        is_shared_(is_shared) {
    current_inst_trace_ = &invalid_instruction_trace;
    if (V8_UNLIKELY(module_ && !module_->inst_traces.empty())) {
      auto last_trace = module_->inst_traces.end() - 1;
      auto first_inst_trace =
          std::lower_bound(module_->inst_traces.begin(), last_trace,
                           std::make_pair(buffer_offset, 0),
                           [](const std::pair<uint32_t, uint32_t>& a,
                              const std::pair<uint32_t, uint32_t>& b) {
                             return a.first < b.first;
                           });
      if (V8_UNLIKELY(first_inst_trace != last_trace)) {
        current_inst_trace_ = &*first_inst_trace;
      }
    }
  }

  Zone* zone() const { return zone_; }

  uint32_t num_locals() const { return num_locals_; }

  base::Vector<ValueType> local_types() const {
    return base::VectorOf(local_types_, num_locals_);
  }
  ValueType local_type(uint32_t index) const {
    DCHECK_GE(num_locals_, index);
    return local_types_[index];
  }

  // Decodes local definitions in the current decoder.
  // The decoded locals will be appended to {this->local_types_}.
  // The decoder's pc is not advanced.
  // The total length of decoded locals is returned.
  uint32_t DecodeLocals(const uint8_t* pc) {
    DCHECK_NULL(local_types_);
    DCHECK_EQ(0, num_locals_);

    // In a first step, count the number of locals and store the decoded
    // entries.
    num_locals_ = static_cast<uint32_t>(this->sig_->parameter_count());

    // Decode local declarations, if any.
    auto [entries, entries_length] =
        read_u32v<ValidationTag>(pc, "local decls count");

    if (!VALIDATE(ok())) {
      DecodeError(pc, "invalid local decls count");
      return 0;
    }
    TRACE("local decls count: %u\n", entries);

    // Do an early validity check, to avoid allocating too much memory below.
    // Every entry needs at least two bytes (count plus type); if that many are
    // not available any more, flag that as an error.
    if (available_bytes() / 2 < entries) {
      DecodeError(pc, "local decls count bigger than remaining function size");
      return 0;
    }

    struct DecodedLocalEntry {
      uint32_t count;
      ValueType type;
    };
    base::SmallVector<DecodedLocalEntry, 8> decoded_locals(entries);
    uint32_t total_length = entries_length;
    for (uint32_t entry = 0; entry < entries; ++entry) {
      if (!VALIDATE(more())) {
        DecodeError(end(),
                    "expected more local decls but reached end of input");
        return 0;
      }

      auto [count, count_length] =
          read_u32v<ValidationTag>(pc + total_length, "local count");
      if (!VALIDATE(ok())) {
        DecodeError(pc + total_length, "invalid local count");
        return 0;
      }
      DCHECK_LE(num_locals_, kV8MaxWasmFunctionLocals);
      if (!VALIDATE(count <= kV8MaxWasmFunctionLocals - num_locals_)) {
        DecodeError(pc + total_length, "local count too large");
        return 0;
      }
      total_length += count_length;

      auto [type, type_length] =
          value_type_reader::read_value_type<ValidationTag>(
              this, pc + total_length, enabled_);
      ValidateValueType(pc + total_length, type);
      if (!VALIDATE(!is_shared_ || IsShared(type, module_))) {
        DecodeError(pc + total_length, "local must have shared type");
        return 0;
      }
      if (!VALIDATE(ok())) return 0;
      total_length += type_length;

      num_locals_ += count;
      decoded_locals[entry] = DecodedLocalEntry{count, type};
    }
    DCHECK(ok());

    if (num_locals_ > 0) {
      // Now build the array of local types from the parsed entries.
      local_types_ = zone_->AllocateArray<ValueType>(num_locals_);
      ValueType* locals_ptr = local_types_;

      if (sig_->parameter_count() > 0) {
        std::copy(sig_->parameters().begin(), sig_->parameters().end(),
                  locals_ptr);
        locals_ptr += sig_->parameter_count();
      }

      for (auto& entry : decoded_locals) {
        std::fill_n(locals_ptr, entry.count, entry.type);
        locals_ptr += entry.count;
      }
      DCHECK_EQ(locals_ptr, local_types_ + num_locals_);
    }
    return total_length;
  }

  // Shorthand that forwards to the {DecodeError} functions above, passing our
  // {ValidationTag}.
  template <typename... Args>
  V8_INLINE void DecodeError(Args... args) {
    wasm::DecodeError<ValidationTag>(this, std::forward<Args>(args)...);
  }

  // Returns a BitVector of length {locals_count + 1} representing the set of
  // variables that are assigned in the loop starting at {pc}. The additional
  // position at the end of the vector represents possible assignments to
  // the instance cache.
  static BitVector* AnalyzeLoopAssignment(WasmDecoder* decoder,
                                          const uint8_t* pc,
                                          uint32_t locals_count, Zone* zone,
                                          bool* loop_is_innermost = nullptr) {
    if (pc >= decoder->end()) return nullptr;
    if (*pc != kExprLoop) return nullptr;
    // The number of locals_count is augmented by 1 so that the 'locals_count'
    // index can be used to track the instance cache.
    BitVector* assigned = zone->New<BitVector>(locals_count + 1, zone);
    int depth = -1;  // We will increment the depth to 0 when we decode the
                     // starting 'loop' opcode.
    if (loop_is_innermost) *loop_is_innermost = true;
    // Iteratively process all AST nodes nested inside the loop.
    while (pc < decoder->end() && VALIDATE(decoder->ok())) {
      WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
      switch (opcode) {
        case kExprLoop:
          if (loop_is_innermost && depth >= 0) *loop_is_innermost = false;
          [[fallthrough]];
        case kExprIf:
        case kExprBlock:
        case kExprTry:
        case kExprTryTable:
          depth++;
          break;
        case kExprLocalSet:
        case kExprLocalTee: {
          IndexImmediate imm(decoder, pc + 1, "local index", validate);
          // Unverified code might have an out-of-bounds index.
          if (imm.index < locals_count) assigned->Add(imm.index);
          break;
        }
        case kExprMemoryGrow:
        case kExprCallFunction:
        case kExprCallIndirect:
        case kExprCallRef:
          // Add instance cache to the assigned set.
          assigned->Add(locals_count);
          break;
        case kExprEnd:
          depth--;
          break;
        default:
          break;
      }
      if (depth < 0) break;
      pc += OpcodeLength(decoder, pc);
    }
    return VALIDATE(decoder->ok()) ? assigned : nullptr;
  }

  bool Validate(const uint8_t* pc, TagIndexImmediate& imm) {
    size_t num_tags = module_->tags.size();
    if (!VALIDATE(imm.index < num_tags)) {
      DecodeError(pc, "Invalid tag index: %u", imm.index);
      return false;
    }
    V8_ASSUME(imm.index < num_tags);
    imm.tag = &module_->tags[imm.index];
    return true;
  }

  bool Validate(const uint8_t* pc, GlobalIndexImmediate& imm) {
    // We compare with the current size of the globals vector. This is important
    // if we are decoding a constant expression in the global section.
    size_t num_globals = module_->globals.size();
    if (!VALIDATE(imm.index < num_globals)) {
      DecodeError(pc, "Invalid global index: %u", imm.index);
      return false;
    }
    V8_ASSUME(imm.index < num_globals);
    imm.global = &module_->globals[imm.index];
    if (!VALIDATE(!is_shared_ || imm.global->shared)) {
      DecodeError(pc, "Cannot access non-shared global %d in a shared %s",
                  imm.index,
                  decoding_mode == kConstantExpression ? "constant expression"
                                                       : "function");
      return false;
    }

    if constexpr (decoding_mode == kConstantExpression) {
      if (!VALIDATE(!imm.global->mutability)) {
        this->DecodeError(pc,
                          "mutable globals cannot be used in constant "
                          "expressions");
        return false;
      }
    }

    return true;
  }

  bool Validate(const uint8_t* pc, SigIndexImmediate& imm) {
    if (!VALIDATE(module_->has_signature(imm.index))) {
      DecodeError(pc, "invalid signature index: %u", imm.index);
      return false;
    }
    imm.sig = module_->signature(imm.index);
    return true;
  }

  bool Validate(const uint8_t* pc, StructIndexImmediate& imm) {
    if (!VALIDATE(module_->has_struct(imm.index))) {
      DecodeError(pc, "invalid struct index: %u", imm.index);
      return false;
    }
    imm.struct_type = module_->struct_type(imm.index);
    return true;
  }

  bool Validate(const uint8_t* pc, FieldImmediate& imm) {
    if (!Validate(pc, imm.struct_imm)) return false;
    if (!VALIDATE(imm.field_imm.index <
                  imm.struct_imm.struct_type->field_count())) {
      DecodeError(pc + imm.struct_imm.length, "invalid field index: %u",
                  imm.field_imm.index);
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, ArrayIndexImmediate& imm) {
    if (!VALIDATE(module_->has_array(imm.index))) {
      DecodeError(pc, "invalid array index: %u", imm.index);
      return false;
    }
    imm.array_type = module_->array_type(imm.index);
    return true;
  }

  bool CanReturnCall(const FunctionSig* target_sig) {
    if (sig_->return_count() != target_sig->return_count()) return false;
    auto target_sig_it = target_sig->returns().begin();
    for (ValueType ret_type : sig_->returns()) {
      if (!IsSubtypeOf(*target_sig_it++, ret_type, this->module_)) return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, CallFunctionImmediate& imm) {
    size_t num_functions = module_->functions.size();
    if (!VALIDATE(imm.index < num_functions)) {
      DecodeError(pc, "function index #%u is out of bounds", imm.index);
      return false;
    }
    if (is_shared_ && !module_->function_is_shared(imm.index)) {
      DecodeError(pc, "cannot call non-shared function %u", imm.index);
      return false;
    }
    V8_ASSUME(imm.index < num_functions);
    imm.sig = module_->functions[imm.index].sig;
    return true;
  }

  bool Validate(const uint8_t* pc, CallIndirectImmediate& imm) {
    if (!ValidateSignature(pc, imm.sig_imm)) return false;
    if (!Validate(pc + imm.sig_imm.length, imm.table_imm)) return false;
    ValueType table_type = imm.table_imm.table->type;
    if (!VALIDATE(IsSubtypeOf(table_type, kWasmFuncRef, module_) ||
                  IsSubtypeOf(table_type,
                              ValueType::RefNull(HeapType::kFuncShared),
                              module_))) {
      DecodeError(
          pc, "call_indirect: immediate table #%u is not of a function type",
          imm.table_imm.index);
      return false;
    }
    // The type specified by the immediate does not need to have any static
    // relation (neither sub nor super) to the type of the table. The type
    // of the function will be checked at runtime.

    imm.sig = module_->signature(imm.sig_imm.index);
    return true;
  }

  bool Validate(const uint8_t* pc, BranchDepthImmediate& imm,
                size_t control_depth) {
    if (!VALIDATE(imm.depth < control_depth)) {
      DecodeError(pc, "invalid branch depth: %u", imm.depth);
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, BranchTableImmediate& imm) {
    if (!VALIDATE(imm.table_count <= kV8MaxWasmFunctionBrTableSize)) {
      DecodeError(pc, "invalid table count (> max br_table size): %u",
                  imm.table_count);
      return false;
    }
    return checkAvailable(imm.table_count);
  }

  bool Validate(const uint8_t* pc, WasmOpcode opcode, SimdLaneImmediate& imm) {
    uint8_t num_lanes = 0;
    switch (opcode) {
      case kExprF64x2ExtractLane:
      case kExprF64x2ReplaceLane:
      case kExprI64x2ExtractLane:
      case kExprI64x2ReplaceLane:
      case kExprS128Load64Lane:
      case kExprS128Store64Lane:
        num_lanes = 2;
        break;
      case kExprF32x4ExtractLane:
      case kExprF32x4ReplaceLane:
      case kExprI32x4ExtractLane:
      case kExprI32x4ReplaceLane:
      case kExprS128Load32Lane:
      case kExprS128Store32Lane:
        num_lanes = 4;
        break;
      case kExprF16x8ExtractLane:
      case kExprF16x8ReplaceLane:
      case kExprI16x8ExtractLaneS:
      case kExprI16x8ExtractLaneU:
      case kExprI16x8ReplaceLane:
      case kExprS128Load16Lane:
      case kExprS128Store16Lane:
        num_lanes = 8;
        break;
      case kExprI8x16ExtractLaneS:
      case kExprI8x16ExtractLaneU:
      case kExprI8x16ReplaceLane:
      case kExprS128Load8Lane:
      case kExprS128Store8Lane:
        num_lanes = 16;
        break;
      default:
        UNREACHABLE();
        break;
    }
    if (!VALIDATE(imm.lane >= 0 && imm.lane < num_lanes)) {
      DecodeError(pc, "invalid lane index");
      return false;
    } else {
      return true;
    }
  }

  bool Validate(const uint8_t* pc, Simd128Immediate& imm) {
    uint8_t max_lane = 0;
    for (uint32_t i = 0; i < kSimd128Size; ++i) {
      max_lane = std::max(max_lane, imm.value[i]);
    }
    // Shuffle indices must be in [0..31] for a 16 lane shuffle.
    if (!VALIDATE(max_lane < 2 * kSimd128Size)) {
      DecodeError(pc, "invalid shuffle mask");
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, BlockTypeImmediate& imm) {
    if (imm.sig.all().begin() == nullptr) {
      // Then use {sig_index} to initialize the signature.
      if (!VALIDATE(module_->has_signature(imm.sig_index))) {
        DecodeError(pc, "block type index %u is not a signature definition",
                    imm.sig_index);
        return false;
      }
      imm.sig = *module_->signature(imm.sig_index);
    } else {
      // Then it's an MVP immediate with 0 parameters and 0-1 returns.
      DCHECK_EQ(0, imm.sig.parameter_count());
      DCHECK_GE(1, imm.sig.return_count());
      if (imm.sig.return_count()) {
        if (!ValidateValueType(pc, imm.sig.GetReturn(0))) return false;
      }
    }
    return true;
  }

  bool Validate(const uint8_t* pc, MemoryIndexImmediate& imm) {
    size_t num_memories = module_->memories.size();
    if (imm.index > 0 || imm.length > 1) {
      this->detected_->add_multi_memory();
    }

    if (!VALIDATE(imm.index < num_memories)) {
      DecodeError(pc,
                  "memory index %u exceeds number of declared memories (%zu)",
                  imm.index, num_memories);
      return false;
    }

    V8_ASSUME(imm.index < num_memories);
    imm.memory = this->module_->memories.data() + imm.index;

    return true;
  }

  bool Validate(const uint8_t* pc, MemoryAccessImmediate& imm) {
    size_t num_memories = module_->memories.size();
    if (!VALIDATE(imm.mem_index < num_memories)) {
      DecodeError(pc,
                  "memory index %u exceeds number of declared memories (%zu)",
                  imm.mem_index, num_memories);
      return false;
    }
    if (!VALIDATE(this->module_->memories[imm.mem_index].is_memory64 ||
                  imm.offset <= kMaxUInt32)) {
      this->DecodeError(pc, "memory offset outside 32-bit range: %" PRIu64,
                        imm.offset);
      return false;
    }

    V8_ASSUME(imm.mem_index < num_memories);
    imm.memory = this->module_->memories.data() + imm.mem_index;

    return true;
  }

  bool Validate(const uint8_t* pc, MemoryInitImmediate& imm) {
    return ValidateDataSegment(pc, imm.data_segment) &&
           Validate(pc + imm.data_segment.length, imm.memory);
  }

  bool Validate(const uint8_t* pc, MemoryCopyImmediate& imm) {
    return Validate(pc, imm.memory_src) &&
           Validate(pc + imm.memory_src.length, imm.memory_dst);
  }

  bool Validate(const uint8_t* pc, TableInitImmediate& imm) {
    if (!ValidateElementSegment(pc, imm.element_segment)) return false;
    if (!Validate(pc + imm.element_segment.length, imm.table)) {
      return false;
    }
    ValueType elem_type =
        module_->elem_segments[imm.element_segment.index].type;
    if (!VALIDATE(IsSubtypeOf(elem_type, imm.table.table->type, module_))) {
      DecodeError(pc, "table %u is not a super-type of %s", imm.table.index,
                  elem_type.name().c_str());
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, TableCopyImmediate& imm) {
    if (!Validate(pc, imm.table_src)) return false;
    if (!Validate(pc + imm.table_src.length, imm.table_dst)) return false;
    ValueType src_type = imm.table_src.table->type;
    if (!VALIDATE(IsSubtypeOf(src_type, imm.table_dst.table->type, module_))) {
      DecodeError(pc, "table %u is not a super-type of %s", imm.table_dst.index,
                  src_type.name().c_str());
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, StringConstImmediate& imm) {
    if (!VALIDATE(imm.index < module_->stringref_literals.size())) {
      DecodeError(pc, "Invalid string literal index: %u", imm.index);
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, TableIndexImmediate& imm) {
    if (imm.index > 0 || imm.length > 1) {
      this->detected_->add_reftypes();
    }
    size_t num_tables = module_->tables.size();
    if (!VALIDATE(imm.index < num_tables)) {
      DecodeError(pc, "table index %u exceeds number of tables (%zu)",
                  imm.index, num_tables);
      return false;
    }
    imm.table = this->module_->tables.data() + imm.index;

    if (!VALIDATE(!is_shared_ || imm.table->shared)) {
      DecodeError(pc,
                  "cannot reference non-shared table %u from shared function",
                  imm.index);
      return false;
    }

    return true;
  }

  // The following Validate* functions all validate an `IndexImmediate`, albeit
  // differently according to context.
  bool ValidateElementSegment(const uint8_t* pc, IndexImmediate& imm) {
    size_t num_elem_segments = module_->elem_segments.size();
    if (!VALIDATE(imm.index < num_elem_segments)) {
      DecodeError(pc, "invalid element segment index: %u", imm.index);
      return false;
    }
    V8_ASSUME(imm.index < num_elem_segments);
    if (!VALIDATE(!is_shared_ || module_->elem_segments[imm.index].shared)) {
      DecodeError(
          pc,
          "cannot reference non-shared element segment %u from shared function",
          imm.index);
      return false;
    }
    return true;
  }

  bool ValidateLocal(const uint8_t* pc, IndexImmediate& imm) {
    if (!VALIDATE(imm.index < num_locals())) {
      DecodeError(pc, "invalid local index: %u", imm.index);
      return false;
    }
    return true;
  }

  bool ValidateType(const uint8_t* pc, IndexImmediate& imm) {
    if (!VALIDATE(module_->has_type(imm.index))) {
      DecodeError(pc, "invalid type index: %u", imm.index);
      return false;
    }
    return true;
  }

  bool ValidateSignature(const uint8_t* pc, IndexImmediate& imm) {
    if (!VALIDATE(module_->has_signature(imm.index))) {
      DecodeError(pc, "invalid signature index: %u", imm.index);
      return false;
    }
    return true;
  }

  bool ValidateFunction(const uint8_t* pc, IndexImmediate& imm) {
    size_t num_functions = module_->functions.size();
    if (!VALIDATE(imm.index < num_functions)) {
      DecodeError(pc, "function index #%u is out of bounds", imm.index);
      return false;
    }
    V8_ASSUME(imm.index < num_functions);
    if (decoding_mode == kFunctionBody &&
        !VALIDATE(module_->functions[imm.index].declared)) {
      DecodeError(pc, "undeclared reference to function #%u", imm.index);
      return false;
    }
    return true;
  }

  bool ValidateDataSegment(const uint8_t* pc, IndexImmediate& imm) {
    if (!VALIDATE(imm.index < module_->num_declared_data_segments)) {
      DecodeError(pc, "invalid data segment index: %u", imm.index);
      return false;
    }
    if (!VALIDATE(!is_shared_ || module_->data_segments[imm.index].shared)) {
      DecodeError(
          pc, "cannot refer to non-shared segment %u from a shared function",
          imm.index);
      return false;
    }
    return true;
  }

  bool Validate(const uint8_t* pc, SelectTypeImmediate& imm) {
    return ValidateValueType(pc, imm.type);
  }

  bool Validate(const uint8_t* pc, HeapTypeImmediate& imm) {
    return ValidateHeapType(pc, imm.type);
  }

  bool ValidateValueType(const uint8_t* pc, ValueType type) {
    return value_type_reader::ValidateValueType<ValidationTag>(this, pc,
                                                               module_, type);
  }

  bool ValidateHeapType(const uint8_t* pc, HeapType type) {
    return value_type_reader::ValidateHeapType<ValidationTag>(this, pc, module_,
                                                              type);
  }

  // Returns the length of the opcode under {pc}.
  template <typename... ImmediateObservers>
  static uint32_t OpcodeLength(WasmDecoder* decoder, const uint8_t* pc,
                               ImmediateObservers&... ios) {
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
    // We don't have information about the module here, so we just assume that
    // memory64 and multi-memory are enabled when parsing memory access
    // immediates. This is backwards-compatible; decode errors will be detected
    // at another time when actually decoding that opcode.
    constexpr bool kConservativelyAssumeMemory64 = true;
    switch (opcode) {
      /********** Control opcodes **********/
      case kExprUnreachable:
      case kExprNop:
      case kExprNopForTestingUnsupportedInLiftoff:
      case kExprElse:
      case kExprEnd:
      case kExprReturn:
        return 1;
      case kExprTry:
      case kExprIf:
      case kExprLoop:
      case kExprBlock: {
        BlockTypeImmediate imm(WasmEnabledFeatures::All(), decoder, pc + 1,
                               validate);
        (ios.BlockType(imm), ...);
        return 1 + imm.length;
      }
      case kExprRethrow:
      case kExprBr:
      case kExprBrIf:
      case kExprBrOnNull:
      case kExprBrOnNonNull:
      case kExprDelegate: {
        BranchDepthImmediate imm(decoder, pc + 1, validate);
        (ios.BranchDepth(imm), ...);
        return 1 + imm.length;
      }
      case kExprBrTable: {
        BranchTableImmediate imm(decoder, pc + 1, validate);
        (ios.BranchTable(imm), ...);
        BranchTableIterator<ValidationTag> iterator(decoder, imm);
        return 1 + iterator.length();
      }
      case kExprTryTable: {
        BlockTypeImmediate block_type_imm(WasmEnabledFeatures::All(), decoder,
                                          pc + 1, validate);
        (ios.BlockType(block_type_imm), ...);
        TryTableImmediate try_table_imm(decoder, pc + 1 + block_type_imm.length,
                                        validate);
        (ios.TryTable(try_table_imm), ...);
        TryTableIterator<ValidationTag> iterator(decoder, try_table_imm);
        return 1 + block_type_imm.length + iterator.length();
      }
      case kExprThrow:
      case kExprCatch: {
        TagIndexImmediate imm(decoder, pc + 1, validate);
        (ios.TagIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprThrowRef:
        return 1;

      /********** Misc opcodes **********/
      case kExprCallFunction:
      case kExprReturnCall: {
        CallFunctionImmediate imm(decoder, pc + 1, validate);
        (ios.FunctionIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprCallIndirect:
      case kExprReturnCallIndirect: {
        CallIndirectImmediate imm(decoder, pc + 1, validate);
        (ios.CallIndirect(imm), ...);
        return 1 + imm.length;
      }
      case kExprCallRef:
      case kExprReturnCallRef: {
        SigIndexImmediate imm(decoder, pc + 1, validate);
        (ios.TypeIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprDrop:
      case kExprSelect:
      case kExprCatchAll:
      case kExprRefEq:
        return 1;
      case kExprSelectWithType: {
        SelectTypeImmediate imm(WasmEnabledFeatures::All(), decoder, pc + 1,
                                validate);
        (ios.SelectType(imm), ...);
        return 1 + imm.length;
      }

      case kExprLocalGet:
      case kExprLocalSet:
      case kExprLocalTee: {
        IndexImmediate imm(decoder, pc + 1, "local index", validate);
        (ios.LocalIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprGlobalGet:
      case kExprGlobalSet: {
        GlobalIndexImmediate imm(decoder, pc + 1, validate);
        (ios.GlobalIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprTableGet:
      case kExprTableSet: {
        TableIndexImmediate imm(decoder, pc + 1, validate);
        (ios.TableIndex(imm), ...);
        return 1 + imm.length;
      }
      case kExprI32Const: {
        ImmI32Immediate imm(decoder, pc + 1, validate);
        (ios.I32Const(imm), ...);
        return 1 + imm.length;
      }
      case kExprI64Const: {
        ImmI64Immediate imm(decoder, pc + 1, validate);
        (ios.I64Const(imm), ...);
        return 1 + imm.length;
      }
      case kExprF32Const:
        if (sizeof...(ios) > 0) {
          ImmF32Immediate imm(decoder, pc + 1, validate);
          (ios.F32Const(imm), ...);
        }
        return 5;
      case kExprF64Const:
        if (sizeof...(ios) > 0) {
          ImmF64Immediate imm(decoder, pc + 1, validate);
          (ios.F64Const(imm), ...);
        }
        return 9;
      case kExprRefNull: {
        HeapTypeImmediate imm(WasmEnabledFeatures::All(), decoder, pc + 1,
                              validate);
        (ios.HeapType(imm), ...);
        return 1 + imm.length;
      }
      case kExprRefIsNull:
      case kExprRefAsNonNull:
        return 1;
      case kExprRefFunc: {
        IndexImmediate imm(decoder, pc + 1, "function index", validate);
        (ios.FunctionIndex(imm), ...);
        return 1 + imm.length;
      }

#define DECLARE_OPCODE_CASE(name, ...) case kExpr##name:
        // clang-format off
      /********** Simple and memory opcodes **********/
      FOREACH_SIMPLE_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_SIMPLE_PROTOTYPE_OPCODE(DECLARE_OPCODE_CASE)
        return 1;
      FOREACH_LOAD_MEM_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_STORE_MEM_OPCODE(DECLARE_OPCODE_CASE) {
        MemoryAccessImmediate imm(decoder, pc + 1, UINT32_MAX,
                                  kConservativelyAssumeMemory64,
                                  validate);
        (ios.MemoryAccess(imm), ...);
        return 1 + imm.length;
      }
      // clang-format on
      case kExprMemoryGrow:
      case kExprMemorySize: {
        MemoryIndexImmediate imm(decoder, pc + 1, validate);
        (ios.MemoryIndex(imm), ...);
        return 1 + imm.length;
      }

      /********** Prefixed opcodes **********/
      case kNumericPrefix: {
        uint32_t length;
        std::tie(opcode, length) =
            decoder->read_prefixed_opcode<ValidationTag>(pc);
        switch (opcode) {
          case kExprI32SConvertSatF32:
          case kExprI32UConvertSatF32:
          case kExprI32SConvertSatF64:
          case kExprI32UConvertSatF64:
          case kExprI64SConvertSatF32:
          case kExprI64UConvertSatF32:
          case kExprI64SConvertSatF64:
          case kExprI64UConvertSatF64:
            return length;
          case kExprMemoryInit: {
            MemoryInitImmediate imm(decoder, pc + length, validate);
            (ios.MemoryInit(imm), ...);
            return length + imm.length;
          }
          case kExprDataDrop: {
            IndexImmediate imm(decoder, pc + length, "data segment index",
                               validate);
            (ios.DataSegmentIndex(imm), ...);
            return length + imm.length;
          }
          case kExprMemoryCopy: {
            MemoryCopyImmediate imm(decoder, pc + length, validate);
            (ios.MemoryCopy(imm), ...);
            return length + imm.length;
          }
          case kExprMemoryFill: {
            MemoryIndexImmediate imm(decoder, pc + length, validate);
            (ios.MemoryIndex(imm), ...);
            return length + imm.length;
          }
          case kExprTableInit: {
            TableInitImmediate imm(decoder, pc + length, validate);
            (ios.TableInit(imm), ...);
            return length + imm.length;
          }
          case kExprElemDrop: {
            IndexImmediate imm(decoder, pc + length, "element segment index",
                               validate);
            (ios.ElemSegmentIndex(imm), ...);
            return length + imm.length;
          }
          case kExprTableCopy: {
            TableCopyImmediate imm(decoder, pc + length, validate);
            (ios.TableCopy(imm), ...);
            return length + imm.length;
          }
          case kExprTableGrow:
          case kExprTableSize:
          case kExprTableFill: {
            TableIndexImmediate imm(decoder, pc + length, validate);
            (ios.TableIndex(imm), ...);
            return length + imm.length;
          }
          case kExprF32LoadMemF16:
          case kExprF32StoreMemF16: {
            MemoryAccessImmediate imm(decoder, pc + length, UINT32_MAX,
                                      kConservativelyAssumeMemory64, validate);
            (ios.MemoryAccess(imm), ...);
            return length + imm.length;
          }
          default:
            // This path is only possible if we are validating.
            V8_ASSUME(ValidationTag::validate);
            decoder->DecodeError(pc, "invalid numeric opcode");
            return length;
        }
      }
      case kSimdPrefix: {
        uint32_t length;
        std::tie(opcode, length) =
            decoder->read_prefixed_opcode<ValidationTag>(pc);
        switch (opcode) {
          // clang-format off
          FOREACH_SIMD_0_OPERAND_OPCODE(DECLARE_OPCODE_CASE)
            return length;
          FOREACH_SIMD_1_OPERAND_OPCODE(DECLARE_OPCODE_CASE)
        if (sizeof...(ios) > 0) {
              SimdLaneImmediate lane_imm(decoder, pc + length, validate);
             (ios.SimdLane(lane_imm), ...);
            }
            return length + 1;
          FOREACH_SIMD_MEM_OPCODE(DECLARE_OPCODE_CASE) {
            MemoryAccessImmediate imm(decoder, pc + length, UINT32_MAX,
                                      kConservativelyAssumeMemory64,
                                      validate);
            (ios.MemoryAccess(imm), ...);
            return length + imm.length;
          }
          FOREACH_SIMD_MEM_1_OPERAND_OPCODE(DECLARE_OPCODE_CASE) {
            MemoryAccessImmediate imm(
                decoder, pc + length, UINT32_MAX,
                kConservativelyAssumeMemory64,
                validate);
        if (sizeof...(ios) > 0) {
              SimdLaneImmediate lane_imm(decoder,
                                         pc + length + imm.length, validate);
             (ios.MemoryAccess(imm), ...);
             (ios.SimdLane(lane_imm), ...);
            }
            // 1 more byte for lane index immediate.
            return length + imm.length + 1;
          }
          // clang-format on
          // Shuffles require a byte per lane, or 16 immediate bytes.
          case kExprS128Const:
          case kExprI8x16Shuffle:
            if (sizeof...(ios) > 0) {
              Simd128Immediate imm(decoder, pc + length, validate);
              (ios.S128Const(imm), ...);
            }
            return length + kSimd128Size;
          default:
            // This path is only possible if we are validating.
            V8_ASSUME(ValidationTag::validate);
            decoder->DecodeError(pc, "invalid SIMD opcode");
            return length;
        }
      }
      case kAtomicPrefix: {
        uint32_t length;
        std::tie(opcode, length) =
            decoder->read_prefixed_opcode<ValidationTag>(pc, "atomic_index");
        switch (opcode) {
          FOREACH_ATOMIC_OPCODE(DECLARE_OPCODE_CASE) {
            MemoryAccessImmediate imm(decoder, pc + length, UINT32_MAX,
                                      kConservativelyAssumeMemory64, validate);
            (ios.MemoryAccess(imm), ...);
            return length + imm.length;
          }
          FOREACH_ATOMIC_0_OPERAND_OPCODE(DECLARE_OPCODE_CASE) {
            // One unused zero-byte.
            return length + 1;
          }
          default:
            // This path is only possible if we are validating.
            V8_ASSUME(ValidationTag::validate);
            decoder->DecodeError(pc, "invalid Atomics opcode");
            return length;
        }
      }
      case kGCPrefix: {
        uint32_t length;
        std::tie(opcode, length) =
            decoder->read_prefixed_opcode<ValidationTag>(pc, "gc_index");
        switch (opcode) {
          case kExprStructNew:
          case kExprStructNewDefault: {
            StructIndexImmediate imm(decoder, pc + length, validate);
            (ios.TypeIndex(imm), ...);
            return length + imm.length;
          }
          case kExprStructGet:
          case kExprStructGetS:
          case kExprStructGetU:
          case kExprStructSet: {
            FieldImmediate imm(decoder, pc + length, validate);
            (ios.Field(imm), ...);
            return length + imm.length;
          }
          case kExprArrayNew:
          case kExprArrayNewDefault:
          case kExprArrayGet:
          case kExprArrayGetS:
          case kExprArrayGetU:
          case kExprArraySet: {
            ArrayIndexImmediate imm(decoder, pc + length, validate);
            (ios.TypeIndex(imm), ...);
            return length + imm.length;
          }
          case kExprArrayNewFixed: {
            ArrayIndexImmediate array_imm(decoder, pc + length, validate);
            IndexImmediate length_imm(decoder, pc + length + array_imm.length,
                                      "array length", validate);
            (ios.TypeIndex(array_imm), ...);
            (ios.Length(length_imm), ...);
            return length + array_imm.length + length_imm.length;
          }
          case kExprArrayCopy: {
            ArrayIndexImmediate dst_imm(decoder, pc + length, validate);
            ArrayIndexImmediate src_imm(decoder, pc + length + dst_imm.length,
                                        validate);
            (ios.ArrayCopy(dst_imm, src_imm), ...);
            return length + dst_imm.length + src_imm.length;
          }
          case kExprArrayFill: {
            ArrayIndexImmediate imm(decoder, pc + length, validate);
            (ios.TypeIndex(imm), ...);
            return length + imm.length;
          }
          case kExprArrayNewData:
          case kExprArrayNewElem:
          case kExprArrayInitData:
          case kExprArrayInitElem: {
            ArrayIndexImmediate array_imm(decoder, pc + length, validate);
            IndexImmediate data_imm(decoder, pc + length + array_imm.length,
                                    "segment index", validate);
            (ios.TypeIndex(array_imm), ...);
            (ios.DataSegmentIndex(data_imm), ...);
            return length + array_imm.length + data_imm.length;
          }
          case kExprRefCast:
          case kExprRefCastNull:
          case kExprRefTest:
          case kExprRefTestNull: {
            HeapTypeImmediate imm(WasmEnabledFeatures::All(), decoder,
                                  pc + length, validate);
            (ios.HeapType(imm), ...);
            return length + imm.length;
          }
          case kExprRefCastNop: {
            IndexImmediate imm(decoder, pc + length, "type index", validate);
            (ios.TypeIndex(imm), ...);
            return length + imm.length;
          }
          case kExprBrOnCast:
          case kExprBrOnCastFail: {
            BrOnCastImmediate flags_imm(decoder, pc + length, validate);
            BranchDepthImmediate branch(decoder, pc + length + flags_imm.length,
                                        validate);
            HeapTypeImmediate source_imm(
                WasmEnabledFeatures::All(), decoder,
                pc + length + flags_imm.length + branch.length, validate);
            HeapTypeImmediate target_imm(WasmEnabledFeatures::All(), decoder,
                                         pc + length + flags_imm.length +
                                             branch.length + source_imm.length,
                                         validate);
            (ios.BranchDepth(branch), ...);
            (ios.ValueType(source_imm, flags_imm.flags.src_is_null), ...);
            (ios.ValueType(target_imm, flags_imm.flags.res_is_null), ...);
            return length + flags_imm.length + branch.length +
                   source_imm.length + target_imm.length;
          }
          case kExprRefI31:
          case kExprI31GetS:
          case kExprI31GetU:
          case kExprAnyConvertExtern:
          case kExprExternConvertAny:
          case kExprArrayLen:
            return length;
          case kExprStringNewUtf8:
          case kExprStringNewUtf8Try:
          case kExprStringNewLossyUtf8:
          case kExprStringNewWtf8:
          case kExprStringEncodeUtf8:
          case kExprStringEncodeLossyUtf8:
          case kExprStringEncodeWtf8:
          case kExprStringViewWtf8EncodeUtf8:
          case kExprStringViewWtf8EncodeLossyUtf8:
          case kExprStringViewWtf8EncodeWtf8:
          case kExprStringNewWtf16:
          case kExprStringEncodeWtf16:
          case kExprStringViewWtf16Encode: {
            MemoryIndexImmediate imm(decoder, pc + length, validate);
            (ios.MemoryIndex(imm), ...);
            return length + imm.length;
          }
          case kExprStringConst: {
            StringConstImmediate imm(decoder, pc + length, validate);
            (ios.StringConst(imm), ...);
            return length + imm.length;
          }
          case kExprStringMeasureUtf8:
          case kExprStringMeasureWtf8:
          case kExprStringNewUtf8Array:
          case kExprStringNewUtf8ArrayTry:
          case kExprStringNewLossyUtf8Array:
          case kExprStringNewWtf8Array:
          case kExprStringEncodeUtf8Array:
          case kExprStringEncodeLossyUtf8Array:
          case kExprStringEncodeWtf8Array:
          case kExprStringMeasureWtf16:
          case kExprStringConcat:
          case kExprStringEq:
          case kExprStringIsUSVSequence:
          case kExprStringAsWtf8:
          case kExprStringViewWtf8Advance:
          case kExprStringViewWtf8Slice:
          case kExprStringAsWtf16:
          case kExprStringViewWtf16Length:
          case kExprStringViewWtf16GetCodeunit:
          case kExprStringViewWtf16Slice:
          case kExprStringAsIter:
          case kExprStringViewIterNext:
          case kExprStringViewIterAdvance:
          case kExprStringViewIterRewind:
          case kExprStringViewIterSlice:
          case kExprStringNewWtf16Array:
          case kExprStringEncodeWtf16Array:
          case kExprStringCompare:
          case kExprStringFromCodePoint:
          case kExprStringHash:
            return length;
          default:
            // This path is only possible if we are validating.
            V8_ASSUME(ValidationTag::validate);
            decoder->DecodeError(pc, "invalid gc opcode");
            return length;
        }
      }

        // clang-format off
      /********** Asmjs opcodes **********/
      FOREACH_ASMJS_COMPAT_OPCODE(DECLARE_OPCODE_CASE)
        return 1;

      // Prefixed opcodes (already handled, included here for completeness of
      // switch)
      FOREACH_SIMD_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_NUMERIC_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_ATOMIC_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_ATOMIC_0_OPERAND_OPCODE(DECLARE_OPCODE_CASE)
      FOREACH_GC_OPCODE(DECLARE_OPCODE_CASE)
        UNREACHABLE();
        // clang-format on
#undef DECLARE_OPCODE_CASE
    }
    // Invalid modules will reach this point.
    if (ValidationTag::validate) {
      decoder->DecodeError(pc, "invalid opcode");
    }
    return 1;
  }

  static constexpr ValidationTag validate = {};

  Zone* const zone_;

  ValueType* local_types_ = nullptr;
  uint32_t num_locals_ = 0;

  const WasmModule* module_;
  const WasmEnabledFeatures enabled_;
  WasmDetectedFeatures* detected_;
  const FunctionSig* sig_;
  bool is_shared_;
  const std::pair<uint32_t, uint32_t>* current_inst_trace_;
};

// Only call this in contexts where {current_code_reachable_and_ok_} is known to
// hold.
#define CALL_INTERFACE(name, ...)                         \
  do {                                                    \
    DCHECK(!control_.empty());                            \
    DCHECK(current_code_reachable_and_ok_);               \
    DCHECK_EQ(current_code_reachable_and_ok_,             \
              this->ok() && control_.back().reachable()); \
    interface_.name(this, ##__VA_ARGS__);                 \
  } while (false)
#define CALL_INTERFACE_IF_OK_AND_REACHABLE(name, ...)     \
  do {                                                    \
    DCHECK(!control_.empty());                            \
    DCHECK_EQ(current_code_reachable_and_ok_,             \
              this->ok() && control_.back().reachable()); \
    if (V8_LIKELY(current_code_reachable_and_ok_)) {      \
      interface_.name(this, ##__VA_ARGS__);               \
    }                                                     \
  } while (false)
#define CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(name, ...)    \
  do {                                                          \
    DCHECK(!control_.empty());                                  \
    if (VALIDATE(this->ok()) &&                                 \
        (control_.size() == 1 || control_at(1)->reachable())) { \
      interface_.name(this, ##__VA_ARGS__);                     \
    }                                                           \
  } while (false)

// An empty class used in place of a {base::SmallVector} for cases where the
// content is not needed afterwards.
// This is used for implementations which set {kUsesPoppedArgs} to {false}.
class NoVector {
 public:
  // Construct from anything; {NoVector} is always empty.
  template <typename... Ts>
  explicit NoVector(Ts&&...) V8_NOEXCEPT {}

  constexpr std::nullptr_t data() const { return nullptr; }
};

template <typename ValidationTag, typename Interface,
          DecodingMode decoding_mode = kFunctionBody>
class WasmFullDecoder : public WasmDecoder<ValidationTag, decoding_mode> {
  using Value = typename Interface::Value;
  using Control = typename Interface::Control;
  using ArgVector = base::Vector<Value>;
  using PoppedArgVector =
      std::conditional_t<Interface::kUsesPoppedArgs,
                         base::SmallVector<Value, 8>, NoVector>;
  using ReturnVector = base::SmallVector<Value, 2>;

  // All Value types should be trivially copyable for performance. We push, pop,
  // and store them in local variables.
  ASSERT_TRIVIALLY_COPYABLE(Value);

 public:
  template <typename... InterfaceArgs>
  WasmFullDecoder(Zone* zone, const WasmModule* module,
                  WasmEnabledFeatures enabled, WasmDetectedFeatures* detected,
                  const FunctionBody& body, InterfaceArgs&&... interface_args)
      : WasmDecoder<ValidationTag, decoding_mode>(
            zone, module, enabled, detected, body.sig, body.is_shared,
            body.start, body.end, body.offset),
        interface_(std::forward<InterfaceArgs>(interface_args)...),
        stack_(16, zone),
        control_(16, zone) {}

  ~WasmFullDecoder() {
    control_.Reset(this->zone_);
    stack_.Reset(this->zone_);
    locals_initializers_stack_.Reset(this->zone_);
  }

  Interface& interface() { return interface_; }

  void Decode() {
    DCHECK(stack_.empty());
    DCHECK(control_.empty());
    DCHECK_LE(this->pc_, this->end_);
    DCHECK_EQ(this->num_locals(), 0);

    locals_offset_ = this->pc_offset();
    uint32_t locals_length = this->DecodeLocals(this->pc());
    if (!VALIDATE(this->ok())) return TraceFailed();
    this->consume_bytes(locals_length);
    int non_defaultable = 0;
    uint32_t params_count =
        static_cast<uint32_t>(this->sig_->parameter_count());
    for (uint32_t index = params_count; index < this->num_locals(); index++) {
      if (!this->local_type(index).is_defaultable()) non_defaultable++;
      // We need this because reference locals are initialized with null, and
      // later we run a lowering step for null based on {detected_}.
      if (this->local_type(index).is_reference()) {
        this->detected_->add_reftypes();
      }
    }
    this->InitializeInitializedLocalsTracking(non_defaultable);

    // Cannot use CALL_INTERFACE_* macros because control is empty.
    interface().StartFunction(this);
    DecodeFunctionBody();
    // Decoding can fail even without validation, e.g. due to missing Liftoff
    // support.
    if (this->failed()) return TraceFailed();

    if (!VALIDATE(control_.empty())) {
      if (control_.size() > 1) {
        this->DecodeError(control_.back().pc(),
                          "unterminated control structure");
      } else {
        this->DecodeError("function body must end with \"end\" opcode");
      }
      return TraceFailed();
    }
    // Cannot use CALL_INTERFACE_* macros because control is empty.
    interface().FinishFunction(this);
    if (this->failed()) return TraceFailed();

    DCHECK(stack_.empty());
    TRACE("wasm-decode ok\n\n");
  }

  void TraceFailed() {
    if (this->error_.offset()) {
      TRACE("wasm-error module+%-6d func+%d: %s\n\n", this->error_.offset(),
            this->GetBufferRelativeOffset(this->error_.offset()),
            this->error_.message().c_str());
    } else {
      TRACE("wasm-error: %s\n\n", this->error_.message().c_str());
    }
  }

  const char* SafeOpcodeNameAt(const uint8_t* pc) {
    if (!pc) return "<null>";
    if (pc >= this->end_) return "<end>";
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc);
    if (!WasmOpcodes::IsPrefixOpcode(opcode)) {
      return WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(opcode));
    }
    opcode = this->template read_prefixed_opcode<Decoder::FullValidationTag>(pc)
                 .first;
    return WasmOpcodes::OpcodeName(opcode);
  }

  WasmCodePosition position() const {
    int offset = static_cast<int>(this->pc_ - this->start_);
    DCHECK_EQ(this->pc_ - this->start_, offset);  // overflows cannot happen
    return offset;
  }

  uint32_t control_depth() const {
    return static_cast<uint32_t>(control_.size());
  }

  Control* control_at(uint32_t depth) {
    DCHECK_GT(control_.size(), depth);
    return control_.end() - 1 - depth;
  }

  uint32_t stack_size() const { return stack_.size(); }

  Value* stack_value(uint32_t depth) const {
    DCHECK_LT(0, depth);
    DCHECK_GE(stack_.size(), depth);
    return stack_.end() - depth;
  }

  int32_t current_catch() const { return current_catch_; }

  uint32_t control_depth_of_current_catch() const {
    return control_depth() - 1 - current_catch();
  }

  uint32_t pc_relative_offset() const {
    return this->pc_offset() - locals_offset_;
  }

  bool is_local_initialized(uint32_t local_index) {
    DCHECK_GT(this->num_locals_, local_index);
    if (!has_nondefaultable_locals_) return true;
    return initialized_locals_[local_index];
  }

  void set_local_initialized(uint32_t local_index) {
    DCHECK_GT(this->num_locals_, local_index);
    if (!has_nondefaultable_locals_) return;
    // This implicitly covers defaultable locals too (which are always
    // initialized).
    if (is_local_initialized(local_index)) return;
    initialized_locals_[local_index] = true;
    locals_initializers_stack_.push(local_index);
  }

  uint32_t locals_initialization_stack_depth() const {
    return static_cast<uint32_t>(locals_initializers_stack_.size());
  }

  void RollbackLocalsInitialization(Control* c) {
    if (!has_nondefaultable_locals_) return;
    uint32_t previous_stack_height = c->init_stack_depth;
    while (locals_initializers_stack_.size() > previous_stack_height) {
      uint32_t local_index = locals_initializers_stack_.back();
      locals_initializers_stack_.pop();
      initialized_locals_[local_index] = false;
    }
  }

  void InitializeInitializedLocalsTracking(int non_defaultable_locals) {
    has_nondefaultable_locals_ = non_defaultable_locals > 0;
    if (!has_nondefaultable_locals_) return;
    initialized_locals_ =
        this->zone_->template AllocateArray<bool>(this->num_locals_);
    // Parameters are always initialized.
    const size_t num_params = this->sig_->parameter_count();
    std::fill_n(initialized_locals_, num_params, true);
    // Locals are initialized if they are defaultable.
    for (size_t i = num_params; i < this->num_locals_; i++) {
      initialized_locals_[i] = this->local_types_[i].is_defaultable();
    }
    DCHECK(locals_initializers_stack_.empty());
    locals_initializers_stack_.EnsureMoreCapacity(non_defaultable_locals,
                                                  this->zone_);
  }

  void DecodeFunctionBody() {
    TRACE("wasm-decode %p...%p (module+%u, %d bytes)\n", this->start(),
          this->end(), this->pc_offset(),
          static_cast<int>(this->end() - this->start()));

    // Set up initial function block.
    {
      DCHECK(control_.empty());
      constexpr uint32_t kStackDepth = 0;
      constexpr uint32_t kInitStackDepth = 0;
      control_.EnsureMoreCapacity(1, this->zone_);
      control_.emplace_back(this->zone_, kControlBlock, kStackDepth,
                            kInitStackDepth, this->pc_, kReachable);
      Control* c = &control_.back();
      if constexpr (decoding_mode == kFunctionBody) {
        InitMerge(&c->start_merge, 0, nullptr);
        InitMerge(&c->end_merge,
                  static_cast<uint32_t>(this->sig_->return_count()),
                  [this](uint32_t i) {
                    return Value{this->pc_, this->sig_->GetReturn(i)};
                  });
      } else {
        DCHECK_EQ(this->sig_->parameter_count(), 0);
        DCHECK_EQ(this->sig_->return_count(), 1);
        c->start_merge.arity = 0;
        c->end_merge.arity = 1;
        c->end_merge.vals.first = Value{this->pc_, this->sig_->GetReturn(0)};
      }
      CALL_INTERFACE_IF_OK_AND_REACHABLE(StartFunctionBody, c);
    }

    if (V8_LIKELY(this->current_inst_trace_->first == 0)) {
      // Decode the function body.
      while (this->pc_ < this->end_) {
        // Most operations only grow the stack by at least one element (unary
        // and binary operations, local.get, constants, ...). Thus check that
        // there is enough space for those operations centrally, and avoid any
        // bounds checks in those operations.
        stack_.EnsureMoreCapacity(1, this->zone_);
        uint8_t first_byte = *this->pc_;
        WasmOpcode opcode = static_cast<WasmOpcode>(first_byte);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(NextInstruction, opcode);
        int len;
        // Allowing two of the most common decoding functions to get inlined
        // appears to be the sweet spot.
        // Handling _all_ opcodes via a giant switch-statement has been tried
        // and found to be slower than calling through the handler table.
        if (opcode == kExprLocalGet) {
          len = WasmFullDecoder::DecodeLocalGet(this, opcode);
        } else if (opcode == kExprI32Const) {
          len = WasmFullDecoder::DecodeI32Const(this, opcode);
        } else {
          OpcodeHandler handler = GetOpcodeHandler(first_byte);
          len = (*handler)(this, opcode);
        }
        this->pc_ += len;
      }

    } else {
      // Decode the function body.
      while (this->pc_ < this->end_) {
        DCHECK(this->current_inst_trace_->first == 0 ||
               this->current_inst_trace_->first >= this->pc_offset());
        if (V8_UNLIKELY(this->current_inst_trace_->first ==
                        this->pc_offset())) {
          TRACE("Emit trace at 0x%x with ID[0x%x]\n", this->pc_offset(),
                this->current_inst_trace_->second);
          CALL_INTERFACE_IF_OK_AND_REACHABLE(TraceInstruction,
                                             this->current_inst_trace_->second);
          this->current_inst_trace_++;
        }

        // Most operations only grow the stack by at least one element (unary
        // and binary operations, local.get, constants, ...). Thus check that
        // there is enough space for those operations centrally, and avoid any
        // bounds checks in those operations.
        stack_.EnsureMoreCapacity(1, this->zone_);
        uint8_t first_byte = *this->pc_;
        WasmOpcode opcode = static_cast<WasmOpcode>(first_byte);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(NextInstruction, opcode);
        OpcodeHandler handler = GetOpcodeHandler(first_byte);
        int len = (*handler)(this, opcode);
        this->pc_ += len;
      }
    }

    // Even without validation, compilation could fail because of unsupported
    // Liftoff operations. In that case, {pc_} did not necessarily advance until
    // {end_}. Thus do not wrap the next check in {VALIDATE}.
    if (this->pc_ != this->end_) {
      this->DecodeError("Beyond end of code");
    }
  }

  bool HasCatchAll(Control* block) const {
    DCHECK(block->is_try_table());
    return std::any_of(block->catch_cases.begin(), block->catch_cases.end(),
                       [](const struct CatchCase& catch_case) {
                         return catch_case.kind == kCatchAll ||
                                catch_case.kind == kCatchAllRef;
                       });
  }

 private:
  uint32_t locals_offset_ = 0;
  Interface interface_;

  // The value stack, stored as individual pointers for maximum performance.
  FastZoneVector<Value> stack_;

  // Indicates whether the local with the given index is currently initialized.
  // Entries for defaultable locals are meaningless; we have a byte for each
  // local because we expect that the effort required to densify this bit
  // vector would more than offset the memory savings.
  bool* initialized_locals_;
  // Keeps track of initializing assignments to non-defaultable locals that
  // happened, so they can be discarded at the end of the current block.
  // Contains no duplicates, so the size of this stack is bounded (and pre-
  // allocated) to the number of non-defaultable locals in the function.
  FastZoneVector<uint32_t> locals_initializers_stack_;

  // Control stack (blocks, loops, ifs, ...).
  FastZoneVector<Control> control_;

  // Controls whether code should be generated for the current block (basically
  // a cache for {ok() && control_.back().reachable()}).
  bool current_code_reachable_and_ok_ = true;

  // Performance optimization: bail out of any functions dealing with non-
  // defaultable locals early when there are no such locals anyway.
  bool has_nondefaultable_locals_ = true;

  // Depth of the current try block.
  int32_t current_catch_ = -1;

  static Value UnreachableValue(const uint8_t* pc) {
    return Value{pc, kWasmBottom};
  }

  void SetSucceedingCodeDynamicallyUnreachable() {
    Control* current = &control_.back();
    if (current->reachable()) {
      current->reachability = kSpecOnlyReachable;
      current_code_reachable_and_ok_ = false;
    }
  }

  // Mark that the current try-catch block might throw.
  // We only generate catch handlers for blocks that might throw.
  void MarkMightThrow() {
    if (!current_code_reachable_and_ok_ || current_catch() == -1) return;
    control_at(control_depth_of_current_catch())->might_throw = true;
  }

  V8_INLINE ValueType TableIndexType(const WasmTable* table) {
    return table->is_table64 ? kWasmI64 : kWasmI32;
  }

  V8_INLINE ValueType MemoryIndexType(const WasmMemory* memory) {
    return memory->is_memory64 ? kWasmI64 : kWasmI32;
  }

  V8_INLINE MemoryAccessImmediate
  MakeMemoryAccessImmediate(uint32_t pc_offset, uint32_t max_alignment) {
    return MemoryAccessImmediate(this, this->pc_ + pc_offset, max_alignment,
                                 this->enabled_.has_memory64(), validate);
  }

#ifdef DEBUG
  class TraceLine {
   public:
    explicit TraceLine(WasmFullDecoder* decoder) : decoder_(decoder) {
      WasmOpcode opcode = static_cast<WasmOpcode>(*decoder->pc());
      if (!WasmOpcodes::IsPrefixOpcode(opcode)) AppendOpcode(opcode);
    }

    void AppendOpcode(WasmOpcode opcode) {
      DCHECK(!WasmOpcodes::IsPrefixOpcode(opcode));
      Append(TRACE_INST_FORMAT, decoder_->startrel(decoder_->pc_),
             WasmOpcodes::OpcodeName(opcode));
    }

    ~TraceLine() {
      if (!v8_flags.trace_wasm_decoder) return;
      AppendStackState();
      PrintF("%.*s\n", len_, buffer_);
    }

    // Appends a formatted string.
    PRINTF_FORMAT(2, 3)
    void Append(const char* format, ...) {
      if (!v8_flags.trace_wasm_decoder) return;
      va_list va_args;
      va_start(va_args, format);
      size_t remaining_len = kMaxLen - len_;
      base::Vector<char> remaining_msg_space(buffer_ + len_, remaining_len);
      int len = base::VSNPrintF(remaining_msg_space, format, va_args);
      va_end(va_args);
      len_ += len < 0 ? remaining_len : len;
    }

   private:
    void AppendStackState() {
      DCHECK(v8_flags.trace_wasm_decoder);
      Append(" ");
      for (Control& c : decoder_->control_) {
        switch (c.kind) {
          case kControlIf:
            Append("I");
            break;
          case kControlBlock:
            Append("B");
            break;
          case kControlLoop:
            Append("L");
            break;
          case kControlTry:
            Append("T");
            break;
          case kControlTryTable:
            Append("T");
            break;
          case kControlIfElse:
            Append("E");
            break;
          case kControlTryCatch:
            Append("C");
            break;
          case kControlTryCatchAll:
            Append("A");
            break;
        }
        if (c.start_merge.arity) Append("%u-", c.start_merge.arity);
        Append("%u", c.end_merge.arity);
        if (!c.reachable()) Append("%c", c.unreachable() ? '*' : '#');
      }
      Append(" | ");
      for (uint32_t i = 0; i < decoder_->stack_.size(); ++i) {
        Value& val = decoder_->stack_[i];
        Append(" %c", val.type.short_name());
      }
    }

    static constexpr int kMaxLen = 512;

    char buffer_[kMaxLen];
    int len_ = 0;
    WasmFullDecoder* const decoder_;
  };
#else
  class TraceLine {
   public:
    explicit TraceLine(WasmFullDecoder*) {}

    void AppendOpcode(WasmOpcode) {}

    PRINTF_FORMAT(2, 3)
    void Append(const char* format, ...) {}
  };
#endif

#define DECODE(name)                                                     \
  static int Decode##name(WasmFullDecoder* decoder, WasmOpcode opcode) { \
    TraceLine trace_msg(decoder);                                        \
    return decoder->Decode##name##Impl(&trace_msg, opcode);              \
  }                                                                      \
  V8_INLINE int Decode##name##Impl(TraceLine* trace_msg, WasmOpcode opcode)

  DECODE(Nop) { return 1; }

  DECODE(NopForTestingUnsupportedInLiftoff) {
    if (!VALIDATE(v8_flags.enable_testing_opcode_in_wasm)) {
      this->DecodeError("Invalid opcode 0x%x", opcode);
      return 0;
    }
    CALL_INTERFACE_IF_OK_AND_REACHABLE(NopForTestingUnsupportedInLiftoff);
    // Return {0} if we failed, to not advance the pc past the end.
    if (this->failed()) {
      DCHECK_EQ(this->pc_, this->end_);
      return 0;
    }
    return 1;
  }

#define BUILD_SIMPLE_OPCODE(op, _, sig, ...) \
  DECODE(op) { return BuildSimpleOperator_##sig(kExpr##op); }
  FOREACH_SIMPLE_NON_CONST_OPCODE(BUILD_SIMPLE_OPCODE)
#undef BUILD_SIMPLE_OPCODE

#define BUILD_SIMPLE_OPCODE(op, _, sig, ...)              \
  DECODE(op) {                                            \
    if constexpr (decoding_mode == kConstantExpression) { \
      this->detected_->add_extended_const();              \
    }                                                     \
    return BuildSimpleOperator_##sig(kExpr##op);          \
  }
  FOREACH_SIMPLE_EXTENDED_CONST_OPCODE(BUILD_SIMPLE_OPCODE)
#undef BUILD_SIMPLE_OPCODE

  DECODE(Block) {
    BlockTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Control* block = PushControl(kControlBlock, imm);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Block, block);
    return 1 + imm.length;
  }

  DECODE(Rethrow) {
    CHECK_PROTOTYPE_OPCODE(legacy_eh);
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm, control_.size())) return 0;
    Control* c = control_at(imm.depth);
    if (!VALIDATE(c->is_try_catchall() || c->is_try_catch())) {
      this->error("rethrow not targeting catch or catch-all");
      return 0;
    }
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Rethrow, c);
    MarkMightThrow();
    EndControl();
    return 1 + imm.length;
  }

  DECODE(Throw) {
    // This instruction is the same for legacy EH and exnref.
    // Count it as exnref if exnref is enabled so that we have an accurate eh
    // count for the deprecation plans.
    this->detected_->Add(this->enabled_.has_exnref()
                             ? WasmDetectedFeature::exnref
                             : WasmDetectedFeature::legacy_eh);
    TagIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    PoppedArgVector args = PopArgs(imm.tag->ToFunctionSig());
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Throw, imm, args.data());
    MarkMightThrow();
    EndControl();
    return 1 + imm.length;
  }

  DECODE(Try) {
    CHECK_PROTOTYPE_OPCODE(legacy_eh);
    BlockTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Control* try_block = PushControl(kControlTry, imm);
    try_block->previous_catch = current_catch_;
    current_catch_ = static_cast<int>(control_depth() - 1);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Try, try_block);
    return 1 + imm.length;
  }

  DECODE(Catch) {
    CHECK_PROTOTYPE_OPCODE(legacy_eh);
    TagIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    DCHECK(!control_.empty());
    Control* c = &control_.back();
    if (!VALIDATE(c->is_try())) {
      this->DecodeError("catch does not match a try");
      return 0;
    }
    if (!VALIDATE(!c->is_try_catchall())) {
      this->DecodeError("catch after catch-all for try");
      return 0;
    }
    FallThrough();
    c->kind = kControlTryCatch;
    stack_.shrink_to(c->stack_depth);
    c->reachability = control_at(1)->innerReachability();
    current_code_reachable_and_ok_ = VALIDATE(this->ok()) && c->reachable();
    RollbackLocalsInitialization(c);
    const WasmTagSig* sig = imm.tag->sig;
    stack_.EnsureMoreCapacity(static_cast<int>(sig->parameter_count()),
                              this->zone_);
    for (ValueType type : sig->parameters()) Push(type);
    base::Vector<Value> values(stack_.begin() + c->stack_depth,
                               sig->parameter_count());
    current_catch_ = c->previous_catch;  // Pop try scope.
    // If there is a throwing instruction in `c`, generate the header for a
    // catch block. Otherwise, the catch block is unreachable.
    if (c->might_throw) {
      CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(CatchException, imm, c, values);
    } else {
      SetSucceedingCodeDynamicallyUnreachable();
    }
    return 1 + imm.length;
  }

  DECODE(Delegate) {
    CHECK_PROTOTYPE_OPCODE(legacy_eh);
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    // -1 because the current try block is not included in the count.
    if (!this->Validate(this->pc_ + 1, imm, control_depth() - 1)) return 0;
    Control* c = &control_.back();
    if (!VALIDATE(c->is_incomplete_try())) {
      this->DecodeError("delegate does not match a try");
      return 0;
    }
    // +1 because the current try block is not included in the count.
    uint32_t target_depth = imm.depth + 1;
    while (target_depth < control_depth() - 1 &&
           (!control_at(target_depth)->is_try() ||
            control_at(target_depth)->is_try_catch() ||
            control_at(target_depth)->is_try_catchall())) {
      target_depth++;
    }
    FallThrough();
    if (c->might_throw) {
      CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(Delegate, target_depth, c);
      // Delegate propagates the `might_throw` status to the delegated-to block.
      if (control_at(1)->reachable() && target_depth != control_depth() - 1) {
        control_at(target_depth)->might_throw = true;
      }
    }
    current_catch_ = c->previous_catch;
    EndControl();
    PopControl();
    return 1 + imm.length;
  }

  DECODE(CatchAll) {
    CHECK_PROTOTYPE_OPCODE(legacy_eh);
    DCHECK(!control_.empty());
    Control* c = &control_.back();
    if (!VALIDATE(c->is_try())) {
      this->DecodeError("catch-all does not match a try");
      return 0;
    }
    if (!VALIDATE(!c->is_try_catchall())) {
      this->error("catch-all already present for try");
      return 0;
    }
    FallThrough();
    c->kind = kControlTryCatchAll;
    c->reachability = control_at(1)->innerReachability();
    current_code_reachable_and_ok_ = VALIDATE(this->ok()) && c->reachable();
    RollbackLocalsInitialization(c);
    current_catch_ = c->previous_catch;  // Pop try scope.
    // If there is a throwing instruction in `c`, generate the header for a
    // catch block. Otherwise, the catch block is unreachable.
    if (c->might_throw) {
      CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(CatchAll, c);
    } else {
      SetSucceedingCodeDynamicallyUnreachable();
    }
    stack_.shrink_to(c->stack_depth);
    return 1;
  }

  DECODE(TryTable) {
    CHECK_PROTOTYPE_OPCODE(exnref);
    BlockTypeImmediate block_imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, block_imm)) return 0;
    Control* try_block = PushControl(kControlTryTable, block_imm);
    TryTableImmediate try_table_imm(this, this->pc_ + 1 + block_imm.length,
                                    validate);
    if (try_table_imm.table_count > 0) {
      try_block->previous_catch = current_catch_;
      current_catch_ = static_cast<int>(control_depth() - 1);
    }
    if (!this->Validate(this->pc_ + 2, try_table_imm)) return 0;
    TryTableIterator<ValidationTag> try_table_iterator(this, try_table_imm);
    try_block->catch_cases = this->zone_->template AllocateVector<CatchCase>(
        try_table_imm.table_count);
    int i = 0;
    while (try_table_iterator.has_next()) {
      CatchCase catch_case = try_table_iterator.next();
      if (!VALIDATE(catch_case.kind <= kLastCatchKind)) {
        this->DecodeError("invalid catch kind in try table");
        return 0;
      }
      if ((catch_case.kind == kCatch || catch_case.kind == kCatchRef) &&
          !this->Validate(this->pc_, catch_case.maybe_tag.tag_imm)) {
        return 0;
      }
      catch_case.br_imm.depth += 1;
      if (!this->Validate(this->pc_, catch_case.br_imm, control_.size())) {
        return 0;
      }

      uint32_t stack_size = stack_.size();
      uint32_t push_count = 0;
      if (catch_case.kind == kCatch || catch_case.kind == kCatchRef) {
        const WasmTagSig* sig = catch_case.maybe_tag.tag_imm.tag->sig;
        stack_.EnsureMoreCapacity(static_cast<int>(sig->parameter_count()),
                                  this->zone_);
        for (ValueType type : sig->parameters()) Push(type);
        push_count += sig->parameter_count();
      }
      if (catch_case.kind == kCatchRef || catch_case.kind == kCatchAllRef) {
        stack_.EnsureMoreCapacity(1, this->zone_);
        Push(kWasmExnRef);
        push_count += 1;
      }
      Control* target = control_at(catch_case.br_imm.depth);
      if (!VALIDATE(push_count == target->br_merge()->arity)) {
        this->DecodeError(
            "catch kind generates %d operand%s, target block expects %d",
            push_count, push_count != 1 ? "s" : "", target->br_merge()->arity);
        return 0;
      }
      if (!VALIDATE(
              (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kNo>(
                  target)))) {
        return 0;
      }
      stack_.shrink_to(stack_size);
      DCHECK_LT(i, try_table_imm.table_count);
      try_block->catch_cases[i] = catch_case;
      ++i;
    }
    CALL_INTERFACE_IF_OK_AND_REACHABLE(TryTable, try_block);
    return 1 + block_imm.length + try_table_iterator.length();
  }

  DECODE(ThrowRef) {
    this->detected_->add_exnref();
    Value value = Pop();
    if (!VALIDATE(
            (value.type.kind() == kRef || value.type.kind() == kRefNull) &&
            value.type.heap_type() == HeapType::kExn)) {
      this->DecodeError("invalid type for throw_ref: expected exnref, found %s",
                        value.type.name().c_str());
      return 0;
    }
    CALL_INTERFACE_IF_OK_AND_REACHABLE(ThrowRef, &value);
    MarkMightThrow();
    EndControl();
    return 1;
  }

  DECODE(BrOnNull) {
    this->detected_->add_typed_funcref();
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm, control_.size())) return 0;
    Value ref_object = Pop();
    Control* c = control_at(imm.depth);
    if (!VALIDATE(
            (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kYes>(
                c)))) {
      return 0;
    }
    switch (ref_object.type.kind()) {
      case kBottom:
        // We are in a polymorphic stack. Leave the stack as it is.
        DCHECK(!current_code_reachable_and_ok_);
        [[fallthrough]];
      case kRef:
        // For a non-nullable value, we won't take the branch, and can leave
        // the stack as it is.
        Push(ref_object);
        break;
      case kRefNull: {
        Value* result = Push(ValueType::Ref(ref_object.type.heap_type()));
        // The result of br_on_null has the same value as the argument (but a
        // non-nullable type).
        if (V8_LIKELY(current_code_reachable_and_ok_)) {
          CALL_INTERFACE(BrOnNull, ref_object, imm.depth, false, result);
          c->br_merge()->reached = true;
        }
        break;
      }
      default:
        PopTypeError(0, ref_object, "object reference");
        return 0;
    }
    return 1 + imm.length;
  }

  DECODE(BrOnNonNull) {
    this->detected_->add_typed_funcref();
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm, control_.size())) return 0;
    Value ref_object = Pop();
    if (!VALIDATE(ref_object.type.is_object_reference() ||
                  ref_object.type.is_bottom())) {
      PopTypeError(
          0, ref_object,
          "subtype of ((ref null any), (ref null extern) or (ref null func))");
      return 0;
    }
    // Typechecking the branch and creating the branch merges requires the
    // non-null value on the stack, so we push it temporarily.
    Value* value_on_branch = Push(ref_object.type.AsNonNull());
    Control* c = control_at(imm.depth);
    if (!VALIDATE(
            (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kYes>(
                c)))) {
      return 0;
    }
    switch (ref_object.type.kind()) {
      case kBottom:
        // We are in unreachable code. Do nothing.
        DCHECK(!current_code_reachable_and_ok_);
        break;
      case kRef:
        // For a non-nullable value, we always take the branch.
        if (V8_LIKELY(current_code_reachable_and_ok_)) {
          CALL_INTERFACE(Forward, ref_object, value_on_branch);
          CALL_INTERFACE(BrOrRet, imm.depth);
          // We know that the following code is not reachable, but according
          // to the spec it technically is. Set it to spec-only reachable.
          SetSucceedingCodeDynamicallyUnreachable();
          c->br_merge()->reached = true;
        }
        break;
      case kRefNull: {
        if (V8_LIKELY(current_code_reachable_and_ok_)) {
          CALL_INTERFACE(BrOnNonNull, ref_object, value_on_branch, imm.depth,
                         true);
          c->br_merge()->reached = true;
        }
        break;
      }
      default:
        PopTypeError(0, ref_object, "object reference");
        return 0;
    }
    Drop(*value_on_branch);
    return 1 + imm.length;
  }

  DECODE(Loop) {
    BlockTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Control* block = PushControl(kControlLoop, imm);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Loop, block);
    // Loops have a merge point at block entry, hence push the merge values
    // (Phis in case of TurboFan) after calling the interface.
    // TODO(clemensb): Can we skip this (and the related PushMergeValues in
    // PopControl) for Liftoff?
    PushMergeValues(block, &block->start_merge);
    return 1 + imm.length;
  }

  DECODE(If) {
    BlockTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Value cond = Pop(kWasmI32);
    Control* if_block = PushControl(kControlIf, imm);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(If, cond, if_block);
    return 1 + imm.length;
  }

  DECODE(Else) {
    DCHECK(!control_.empty());
    Control* c = &control_.back();
    if (!VALIDATE(c->is_if())) {
      this->DecodeError("else does not match an if");
      return 0;
    }
    if (!VALIDATE(c->is_onearmed_if())) {
      this->DecodeError("else already present for if");
      return 0;
    }
    if (!VALIDATE(TypeCheckFallThru())) return 0;
    c->kind = kControlIfElse;
    CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(Else, c);
    if (c->reachable()) c->end_merge.reached = true;
    RollbackLocalsInitialization(c);
    PushMergeValues(c, &c->start_merge);
    c->reachability = control_at(1)->innerReachability();
    current_code_reachable_and_ok_ = VALIDATE(this->ok()) && c->reachable();
    return 1;
  }

  DECODE(End) {
    DCHECK(!control_.empty());
    if constexpr (decoding_mode == kFunctionBody) {
      Control* c = &control_.back();
      if (c->is_incomplete_try()) {
        // Catch-less try, fall through to the implicit catch-all.
        c->kind = kControlTryCatch;
        current_catch_ = c->previous_catch;  // Pop try scope.
      }
      if (c->is_try_catch()) {
        // Emulate catch-all + re-throw.
        FallThrough();
        c->reachability = control_at(1)->innerReachability();
        current_code_reachable_and_ok_ = VALIDATE(this->ok()) && c->reachable();
        // Cache `c->might_throw` so we can access it safely after `c`'s
        // destructor is called in `PopContol()`.
        bool might_throw = c->might_throw;
        if (might_throw) {
          CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(CatchAll, c);
          CALL_INTERFACE_IF_OK_AND_REACHABLE(Rethrow, c);
        }
        EndControl();
        PopControl();
        // We must mark the parent catch block as `might_throw`, since this
        // conceptually rethrows. Note that we do this regardless of whether
        // the code at this point is reachable.
        if (might_throw && current_catch() != -1) {
          control_at(control_depth_of_current_catch())->might_throw = true;
        }
        return 1;
      }
      if (c->is_onearmed_if()) {
        if (!VALIDATE(TypeCheckOneArmedIf(c))) return 0;
      }
      if (c->is_try_table()) {
        current_catch_ = c->previous_catch;
        FallThrough();
        // Temporarily set the reachability for the catch handlers, and restore
        // it before we actually exit the try block.
        Reachability reachability_at_end = c->reachability;
        c->reachability = control_at(1)->innerReachability();
        current_code_reachable_and_ok_ = VALIDATE(this->ok()) && c->reachable();
        for (CatchCase& catch_case : c->catch_cases) {
          uint32_t stack_size = stack_.size();
          size_t push_count = 0;
          if (catch_case.kind == kCatch || catch_case.kind == kCatchRef) {
            const WasmTagSig* sig = catch_case.maybe_tag.tag_imm.tag->sig;
            stack_.EnsureMoreCapacity(static_cast<int>(sig->parameter_count()),
                                      this->zone_);
            for (ValueType type : sig->parameters()) Push(type);
            push_count = sig->parameter_count();
          }
          if (catch_case.kind == kCatchRef || catch_case.kind == kCatchAllRef) {
            stack_.EnsureMoreCapacity(1, this->zone_);
            Push(kWasmExnRef);
            push_count += 1;
          }
          base::Vector<Value> values(
              stack_.begin() + stack_.size() - push_count, push_count);
          if (c->might_throw) {
            // Already type checked on block entry.
            CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(CatchCase, c, catch_case,
                                                      values);
            if (current_code_reachable_and_ok_) {
              Control* target = control_at(catch_case.br_imm.depth);
              target->br_merge()->reached = true;
            }
          }
          stack_.shrink_to(stack_size);
          if (catch_case.kind == kCatchAll || catch_case.kind == kCatchAllRef) {
            break;
          }
        }
        c->reachability = reachability_at_end;
        // If there is no catch-all case, we must mark the parent catch block as
        // `might_throw`, since this conceptually rethrows. Note that we do this
        // regardless of whether the code at this point is reachable.
        if (c->might_throw && !HasCatchAll(c) && current_catch() != -1) {
          control_at(control_depth_of_current_catch())->might_throw = true;
        }
        EndControl();
        PopControl();
        return 1;
      }
    }

    if (control_.size() == 1) {
      // We need to call this first because the interface might set
      // {this->end_}, making the next check pass.
      DoReturn<kStrictCounting, decoding_mode == kFunctionBody
                                    ? kFallthroughMerge
                                    : kInitExprMerge>();
      // If at the last (implicit) control, check we are at end.
      if (!VALIDATE(this->pc_ + 1 == this->end_)) {
        this->DecodeError(this->pc_ + 1, "trailing code after function end");
        return 0;
      }
      // The result of the block is the return value.
      trace_msg->Append("\n" TRACE_INST_FORMAT, startrel(this->pc_),
                        "(implicit) return");
      control_.pop();
      return 1;
    }

    if (!VALIDATE(TypeCheckFallThru())) return 0;
    PopControl();
    return 1;
  }

  DECODE(Select) {
    auto [tval, fval, cond] = Pop(kWasmBottom, kWasmBottom, kWasmI32);
    ValueType result_type = tval.type;
    if (result_type == kWasmBottom) {
      result_type = fval.type;
    } else {
      ValidateStackValue(1, fval, result_type);
    }
    if (!VALIDATE(!result_type.is_reference())) {
      this->DecodeError(
          "select without type is only valid for value type inputs");
      return 0;
    }
    Value* result = Push(result_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Select, cond, fval, tval, result);
    return 1;
  }

  DECODE(SelectWithType) {
    this->detected_->add_reftypes();
    SelectTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    auto [tval, fval, cond] = Pop(imm.type, imm.type, kWasmI32);
    Value* result = Push(imm.type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Select, cond, fval, tval, result);
    return 1 + imm.length;
  }

  DECODE(Br) {
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm, control_.size())) return 0;
    Control* c = control_at(imm.depth);
    if (!VALIDATE(
            (TypeCheckBranch<PushBranchValues::kNo, RewriteStackTypes::kNo>(
                c)))) {
      return 0;
    }
    if (V8_LIKELY(current_code_reachable_and_ok_)) {
      CALL_INTERFACE(BrOrRet, imm.depth);
      c->br_merge()->reached = true;
    }
    EndControl();
    return 1 + imm.length;
  }

  DECODE(BrIf) {
    BranchDepthImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm, control_.size())) return 0;
    Value cond = Pop(kWasmI32);
    Control* c = control_at(imm.depth);
    if (!VALIDATE(
            (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kYes>(
                c)))) {
      return 0;
    }
    if (V8_LIKELY(current_code_reachable_and_ok_)) {
      CALL_INTERFACE(BrIf, cond, imm.depth);
      c->br_merge()->reached = true;
    }
    return 1 + imm.length;
  }

  DECODE(BrTable) {
    BranchTableImmediate imm(this, this->pc_ + 1, validate);
    BranchTableIterator<ValidationTag> iterator(this, imm);
    Value key = Pop(kWasmI32);
    if (!VALIDATE(this->ok())) return 0;
    if (!this->Validate(this->pc_ + 1, imm)) return 0;

    // Cache the branch targets during the iteration, so that we can set
    // all branch targets as reachable after the {CALL_INTERFACE} call.
    SmallZoneVector<bool, 32> br_targets(control_.size(), this->zone());
    std::uninitialized_fill(br_targets.begin(), br_targets.end(), false);

    uint32_t arity = 0;

    while (iterator.has_next()) {
      const uint32_t index = iterator.cur_index();
      const uint8_t* pos = iterator.pc();
      const uint32_t target = iterator.next();
      if (!VALIDATE(target < control_depth())) {
        this->DecodeError(pos, "invalid branch depth: %u", target);
        return 0;
      }
      // Avoid redundant branch target checks.
      if (br_targets[target]) continue;
      br_targets[target] = true;

      if (ValidationTag::validate) {
        if (index == 0) {
          arity = control_at(target)->br_merge()->arity;
        } else if (!VALIDATE(control_at(target)->br_merge()->arity == arity)) {
          this->DecodeError(
              pos, "br_table: label arity inconsistent with previous arity %d",
              arity);
          return 0;
        }
        if (!VALIDATE(
                (TypeCheckBranch<PushBranchValues::kNo, RewriteStackTypes::kNo>(
                    control_at(target))))) {
          return 0;
        }
      }
    }

    if (V8_LIKELY(current_code_reachable_and_ok_)) {
      CALL_INTERFACE(BrTable, imm, key);

      for (uint32_t i = 0; i < control_depth(); ++i) {
        control_at(i)->br_merge()->reached |= br_targets[i];
      }
    }
    EndControl();
    return 1 + iterator.length();
  }

  DECODE(Return) {
    return DoReturn<kNonStrictCounting, kReturnMerge>() ? 1 : 0;
  }

  DECODE(Unreachable) {
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Trap, TrapReason::kTrapUnreachable);
    EndControl();
    return 1;
  }

  DECODE(I32Const) {
    ImmI32Immediate imm(this, this->pc_ + 1, validate);
    Value* value = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(I32Const, value, imm.value);
    return 1 + imm.length;
  }

  DECODE(I64Const) {
    ImmI64Immediate imm(this, this->pc_ + 1, validate);
    Value* value = Push(kWasmI64);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(I64Const, value, imm.value);
    return 1 + imm.length;
  }

  DECODE(F32Const) {
    ImmF32Immediate imm(this, this->pc_ + 1, validate);
    Value* value = Push(kWasmF32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(F32Const, value, imm.value);
    return 1 + imm.length;
  }

  DECODE(F64Const) {
    ImmF64Immediate imm(this, this->pc_ + 1, validate);
    Value* value = Push(kWasmF64);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(F64Const, value, imm.value);
    return 1 + imm.length;
  }

  DECODE(RefNull) {
    this->detected_->add_reftypes();
    HeapTypeImmediate imm(this->enabled_, this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(!this->enabled_.has_stringref() ||
                  !imm.type.is_string_view())) {
      this->DecodeError(this->pc_ + 1, "cannot create null string view");
      return 0;
    }
    ValueType type = ValueType::RefNull(imm.type);
    Value* value = Push(type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(RefNull, type, value);
    return 1 + imm.length;
  }

  DECODE(RefIsNull) {
    this->detected_->add_reftypes();
    Value value = Pop();
    Value* result = Push(kWasmI32);
    switch (value.type.kind()) {
      case kRefNull:
        CALL_INTERFACE_IF_OK_AND_REACHABLE(UnOp, kExprRefIsNull, value, result);
        return 1;
      case kBottom:
        // We are in unreachable code, the return value does not matter.
      case kRef:
        // For non-nullable references, the result is always false.
        CALL_INTERFACE_IF_OK_AND_REACHABLE(Drop);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(I32Const, result, 0);
        return 1;
      default:
        if constexpr (!ValidationTag::validate) UNREACHABLE();
        PopTypeError(0, value, "reference type");
        return 0;
    }
  }

  DECODE(RefFunc) {
    this->detected_->add_reftypes();
    IndexImmediate imm(this, this->pc_ + 1, "function index", validate);
    if (!this->ValidateFunction(this->pc_ + 1, imm)) return 0;
    Value* value =
        Push(ValueType::Ref(this->module_->functions[imm.index].sig_index));
    CALL_INTERFACE_IF_OK_AND_REACHABLE(RefFunc, imm.index, value);
    return 1 + imm.length;
  }

  DECODE(RefAsNonNull) {
    this->detected_->add_typed_funcref();
    Value value = Pop();
    switch (value.type.kind()) {
      case kBottom:
        // We are in unreachable code. Forward the bottom value.
      case kRef:
        // A non-nullable value can remain as-is.
        Push(value);
        return 1;
      case kRefNull: {
        Value* result = Push(ValueType::Ref(value.type.heap_type()));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(RefAsNonNull, value, result);
        return 1;
      }
      default:
        if constexpr (!ValidationTag::validate) UNREACHABLE();
        PopTypeError(0, value, "reference type");
        return 0;
    }
  }

  V8_INLINE DECODE(LocalGet) {
    IndexImmediate imm(this, this->pc_ + 1, "local index", validate);
    if (!this->ValidateLocal(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(this->is_local_initialized(imm.index))) {
      this->DecodeError(this->pc_, "uninitialized non-defaultable local: %u",
                        imm.index);
      return 0;
    }
    Value* value = Push(this->local_type(imm.index));
    CALL_INTERFACE_IF_OK_AND_REACHABLE(LocalGet, value, imm);
    return 1 + imm.length;
  }

  DECODE(LocalSet) {
    IndexImmediate imm(this, this->pc_ + 1, "local index", validate);
    if (!this->ValidateLocal(this->pc_ + 1, imm)) return 0;
    Value value = Pop(this->local_type(imm.index));
    CALL_INTERFACE_IF_OK_AND_REACHABLE(LocalSet, value, imm);
    this->set_local_initialized(imm.index);
    return 1 + imm.length;
  }

  DECODE(LocalTee) {
    IndexImmediate imm(this, this->pc_ + 1, "local index", validate);
    if (!this->ValidateLocal(this->pc_ + 1, imm)) return 0;
    ValueType local_type = this->local_type(imm.index);
    Value value = Pop(local_type);
    Value* result = Push(local_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(LocalTee, value, result, imm);
    this->set_local_initialized(imm.index);
    return 1 + imm.length;
  }

  DECODE(Drop) {
    Pop();
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Drop);
    return 1;
  }

  DECODE(GlobalGet) {
    GlobalIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Value* result = Push(imm.global->type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(GlobalGet, result, imm);
    return 1 + imm.length;
  }

  DECODE(GlobalSet) {
    GlobalIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(imm.global->mutability)) {
      this->DecodeError("immutable global #%u cannot be assigned", imm.index);
      return 0;
    }
    Value value = Pop(imm.global->type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(GlobalSet, value, imm);
    return 1 + imm.length;
  }

  DECODE(TableGet) {
    this->detected_->add_reftypes();
    TableIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Value index = Pop(TableIndexType(imm.table));
    Value* result = Push(imm.table->type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(TableGet, index, result, imm);
    return 1 + imm.length;
  }

  DECODE(TableSet) {
    this->detected_->add_reftypes();
    TableIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    ValueType table_index_type = TableIndexType(imm.table);
    auto [index, value] = Pop(table_index_type, imm.table->type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(TableSet, index, value, imm);
    return 1 + imm.length;
  }

  DECODE(LoadMem) { return DecodeLoadMem(GetLoadType(opcode)); }

  DECODE(StoreMem) { return DecodeStoreMem(GetStoreType(opcode)); }

  DECODE(MemoryGrow) {
    // This opcode will not be emitted by the asm translator.
    DCHECK_EQ(kWasmOrigin, this->module_->origin);
    MemoryIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    ValueType mem_type = MemoryIndexType(imm.memory);
    Value value = Pop(mem_type);
    Value* result = Push(mem_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(MemoryGrow, imm, value, result);
    return 1 + imm.length;
  }

  DECODE(MemorySize) {
    MemoryIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    ValueType result_type = MemoryIndexType(imm.memory);
    Value* result = Push(result_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(CurrentMemoryPages, imm, result);
    return 1 + imm.length;
  }

  DECODE(CallFunction) {
    CallFunctionImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    PoppedArgVector args = PopArgs(imm.sig);
    Value* returns = PushReturns(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(CallDirect, imm, args.data(), returns);
    MarkMightThrow();
    return 1 + imm.length;
  }

  DECODE(CallIndirect) {
    CallIndirectImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Value index = Pop(TableIndexType(imm.table_imm.table));
    PoppedArgVector args = PopArgs(imm.sig);
    Value* returns = PushReturns(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(CallIndirect, index, imm, args.data(),
                                       returns);
    MarkMightThrow();
    if (!this->module_->types[imm.sig_imm.index].is_final) {
      // In this case we emit an rtt.canon as part of the indirect call.
      this->detected_->add_gc();
    }
    return 1 + imm.length;
  }

  DECODE(ReturnCall) {
    this->detected_->add_return_call();
    CallFunctionImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(this->CanReturnCall(imm.sig))) {
      this->DecodeError("%s: %s", WasmOpcodes::OpcodeName(kExprReturnCall),
                        "tail call type error");
      return 0;
    }
    PoppedArgVector args = PopArgs(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(ReturnCall, imm, args.data());
    EndControl();
    return 1 + imm.length;
  }

  DECODE(ReturnCallIndirect) {
    this->detected_->add_return_call();
    CallIndirectImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(this->CanReturnCall(imm.sig))) {
      this->DecodeError("%s: %s",
                        WasmOpcodes::OpcodeName(kExprReturnCallIndirect),
                        "tail call return types mismatch");
      return 0;
    }
    Value index = Pop(TableIndexType(imm.table_imm.table));
    PoppedArgVector args = PopArgs(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(ReturnCallIndirect, index, imm,
                                       args.data());
    EndControl();
    if (!this->module_->types[imm.sig_imm.index].is_final) {
      // In this case we emit an rtt.canon as part of the indirect call.
      this->detected_->add_gc();
    }
    return 1 + imm.length;
  }

  DECODE(CallRef) {
    this->detected_->add_typed_funcref();
    SigIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    Value func_ref = Pop(ValueType::RefNull(imm.index));
    PoppedArgVector args = PopArgs(imm.sig);
    Value* returns = PushReturns(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(CallRef, func_ref, imm.sig, args.data(),
                                       returns);
    MarkMightThrow();
    return 1 + imm.length;
  }

  DECODE(ReturnCallRef) {
    this->detected_->add_typed_funcref();
    this->detected_->add_return_call();
    SigIndexImmediate imm(this, this->pc_ + 1, validate);
    if (!this->Validate(this->pc_ + 1, imm)) return 0;
    if (!VALIDATE(this->CanReturnCall(imm.sig))) {
      this->DecodeError("%s: %s", WasmOpcodes::OpcodeName(kExprReturnCallRef),
                        "tail call return types mismatch");
      return 0;
    }
    Value func_ref = Pop(ValueType::RefNull(imm.index));
    PoppedArgVector args = PopArgs(imm.sig);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(ReturnCallRef, func_ref, imm.sig,
                                       args.data());
    EndControl();
    return 1 + imm.length;
  }

  DECODE(RefEq) {
    this->detected_->add_gc();
    Value lhs = Pop();
    if (!VALIDATE(IsSubtypeOf(lhs.type, kWasmEqRef, this->module_) ||
                  IsSubtypeOf(lhs.type, ValueType::RefNull(HeapType::kEqShared),
                              this->module_) ||
                  control_.back().unreachable())) {
      this->DecodeError(this->pc_,
                        "ref.eq[0] expected either eqref or (ref null shared "
                        "eq), found %s of type %s",
                        SafeOpcodeNameAt(lhs.pc()), lhs.type.name().c_str());
    }
    Value rhs = Pop();
    if (!VALIDATE(IsSubtypeOf(rhs.type, kWasmEqRef, this->module_) ||
                  IsSubtypeOf(rhs.type, ValueType::RefNull(HeapType::kEqShared),
                              this->module_) ||
                  control_.back().unreachable())) {
      this->DecodeError(this->pc_,
                        "ref.eq[0] expected either eqref or (ref null shared "
                        "eq), found %s of type %s",
                        SafeOpcodeNameAt(rhs.pc()), rhs.type.name().c_str());
    }
    Value* result = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(BinOp, kExprRefEq, lhs, rhs, result);
    return 1;
  }

  DECODE(Numeric) {
    auto [full_opcode, opcode_length] =
        this->template read_prefixed_opcode<ValidationTag>(this->pc_,
                                                           "numeric index");
    if (full_opcode == kExprTableGrow || full_opcode == kExprTableSize ||
        full_opcode == kExprTableFill) {
      this->detected_->add_reftypes();
    }
    trace_msg->AppendOpcode(full_opcode);
    return DecodeNumericOpcode(full_opcode, opcode_length);
  }

  DECODE(Simd) {
    this->detected_->add_simd();
    if (!CheckHardwareSupportsSimd()) {
      if (v8_flags.correctness_fuzzer_suppressions) {
        FATAL("Aborting on missing Wasm SIMD support");
      }
      this->DecodeError("Wasm SIMD unsupported");
      return 0;
    }
    auto [full_opcode, opcode_length] =
        this->template read_prefixed_opcode<ValidationTag>(this->pc_);
    if (!VALIDATE(this->ok())) return 0;
    trace_msg->AppendOpcode(full_opcode);
    if (WasmOpcodes::IsFP16SimdOpcode(full_opcode)) {
      this->detected_->add_fp16();
    } else if (WasmOpcodes::IsRelaxedSimdOpcode(full_opcode)) {
      this->detected_->add_relaxed_simd();
    }
    return DecodeSimdOpcode(full_opcode, opcode_length);
  }

  DECODE(Atomic) {
    this->detected_->add_threads();
    auto [full_opcode, opcode_length] =
        this->template read_prefixed_opcode<ValidationTag>(this->pc_,
                                                           "atomic index");
    trace_msg->AppendOpcode(full_opcode);
    return DecodeAtomicOpcode(full_opcode, opcode_length);
  }

  DECODE(GC) {
    auto [full_opcode, opcode_length] =
        this->template read_prefixed_opcode<ValidationTag>(this->pc_,
                                                           "gc index");
    trace_msg->AppendOpcode(full_opcode);
    // If we are validating we could have read an illegal opcode. Handle that
    // separately.
    if (!VALIDATE(full_opcode != 0)) {
      DCHECK(this->failed());
      return 0;
    } else if (full_opcode >= kExprStringNewUtf8) {
      CHECK_PROTOTYPE_OPCODE(stringref);
      return DecodeStringRefOpcode(full_opcode, opcode_length);
    } else {
      this->detected_->add_gc();
      return DecodeGCOpcode(full_opcode, opcode_length);
    }
  }

#define SIMPLE_PROTOTYPE_CASE(name, ...) \
  DECODE(name) { return BuildSimplePrototypeOperator(opcode); }
  FOREACH_SIMPLE_PROTOTYPE_OPCODE(SIMPLE_PROTOTYPE_CASE)
#undef SIMPLE_PROTOTYPE_CASE

  DECODE(UnknownOrAsmJs) {
    // Deal with special asmjs opcodes.
    if (!VALIDATE(is_asmjs_module(this->module_))) {
      this->DecodeError("Invalid opcode 0x%x", opcode);
      return 0;
    }
    const FunctionSig* sig = WasmOpcodes::AsmjsSignature(opcode);
    DCHECK_NOT_NULL(sig);
    return BuildSimpleOperator(opcode, sig);
  }

#undef DECODE

  static int NonConstError(WasmFullDecoder* decoder, WasmOpcode opcode) {
    decoder->DecodeError("opcode %s is not allowed in constant expressions",
                         WasmOpcodes::OpcodeName(opcode));
    return 0;
  }

  using OpcodeHandler = int (*)(WasmFullDecoder*, WasmOpcode);

  // Ideally we would use template specialization for the different opcodes, but
  // GCC does not allow to specialize templates in class scope
  // (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85282), and specializing
  // outside the class is not allowed for non-specialized classes.
  // Hence just list all implementations explicitly here, which also gives more
  // freedom to use the same implementation for different opcodes.
#define DECODE_IMPL(opcode) DECODE_IMPL2(kExpr##opcode, opcode)
#define DECODE_IMPL2(opcode, name)                        \
  if (idx == opcode) {                                    \
    if constexpr (decoding_mode == kConstantExpression) { \
      return &WasmFullDecoder::NonConstError;             \
    } else {                                              \
      return &WasmFullDecoder::Decode##name;              \
    }                                                     \
  }
#define DECODE_IMPL_CONST(opcode) DECODE_IMPL_CONST2(kExpr##opcode, opcode)
#define DECODE_IMPL_CONST2(opcode, name) \
  if (idx == opcode) return &WasmFullDecoder::Decode##name

  static constexpr OpcodeHandler GetOpcodeHandlerTableEntry(size_t idx) {
    DECODE_IMPL(Nop);
#define BUILD_SIMPLE_OPCODE(op, ...) DECODE_IMPL(op);
    FOREACH_SIMPLE_NON_CONST_OPCODE(BUILD_SIMPLE_OPCODE)
#undef BUILD_SIMPLE_OPCODE
#define BUILD_SIMPLE_EXTENDED_CONST_OPCODE(op, ...) DECODE_IMPL_CONST(op);
    FOREACH_SIMPLE_EXTENDED_CONST_OPCODE(BUILD_SIMPLE_EXTENDED_CONST_OPCODE)
#undef BUILD_SIMPLE_EXTENDED_CONST_OPCODE
    DECODE_IMPL(Block);
    DECODE_IMPL(Rethrow);
    DECODE_IMPL(Throw);
    DECODE_IMPL(Try);
    DECODE_IMPL(TryTable);
    DECODE_IMPL(ThrowRef);
    DECODE_IMPL(Catch);
    DECODE_IMPL(Delegate);
    DECODE_IMPL(CatchAll);
    DECODE_IMPL(BrOnNull);
    DECODE_IMPL(BrOnNonNull);
    DECODE_IMPL(Loop);
    DECODE_IMPL(If);
    DECODE_IMPL(Else);
    DECODE_IMPL_CONST(End);
    DECODE_IMPL(Select);
    DECODE_IMPL(SelectWithType);
    DECODE_IMPL(Br);
    DECODE_IMPL(BrIf);
    DECODE_IMPL(BrTable);
    DECODE_IMPL(Return);
    DECODE_IMPL(Unreachable);
    DECODE_IMPL(NopForTestingUnsupportedInLiftoff);
    DECODE_IMPL_CONST(I32Const);
    DECODE_IMPL_CONST(I64Const);
    DECODE_IMPL_CONST(F32Const);
    DECODE_IMPL_CONST(F64Const);
    DECODE_IMPL_CONST(RefNull);
    DECODE_IMPL(RefIsNull);
    DECODE_IMPL_CONST(RefFunc);
    DECODE_IMPL(RefAsNonNull);
    DECODE_IMPL(RefEq);
    DECODE_IMPL(LocalGet);
    DECODE_IMPL(LocalSet);
    DECODE_IMPL(LocalTee);
    DECODE_IMPL(Drop);
    DECODE_IMPL_CONST(GlobalGet);
    DECODE_IMPL(GlobalSet);
    DECODE_IMPL(TableGet);
    DECODE_IMPL(TableSet);
#define DECODE_LOAD_MEM(op, ...) DECODE_IMPL2(kExpr##op, LoadMem);
    FOREACH_LOAD_MEM_OPCODE(DECODE_LOAD_MEM)
#undef DECODE_LOAD_MEM
#define DECODE_STORE_MEM(op, ...) DECODE_IMPL2(kExpr##op, StoreMem);
    FOREACH_STORE_MEM_OPCODE(DECODE_STORE_MEM)
#undef DECODE_LOAD_MEM
    DECODE_IMPL(MemoryGrow);
    DECODE_IMPL(MemorySize);
    DECODE_IMPL(CallFunction);
    DECODE_IMPL(CallIndirect);
    DECODE_IMPL(ReturnCall);
    DECODE_IMPL(ReturnCallIndirect);
    DECODE_IMPL(CallRef);
    DECODE_IMPL(ReturnCallRef);
    DECODE_IMPL2(kNumericPrefix, Numeric);
    DECODE_IMPL_CONST2(kSimdPrefix, Simd);
    DECODE_IMPL2(kAtomicPrefix, Atomic);
    DECODE_IMPL_CONST2(kGCPrefix, GC);
#define SIMPLE_PROTOTYPE_CASE(name, ...) DECODE_IMPL(name);
    FOREACH_SIMPLE_PROTOTYPE_OPCODE(SIMPLE_PROTOTYPE_CASE)
#undef SIMPLE_PROTOTYPE_CASE
    return &WasmFullDecoder::DecodeUnknownOrAsmJs;
  }

#undef DECODE_IMPL
#undef DECODE_IMPL2

  OpcodeHandler GetOpcodeHandler(uint8_t opcode) {
    static constexpr std::array<OpcodeHandler, 256> kOpcodeHandlers =
        base::make_array<256>(GetOpcodeHandlerTableEntry);
    return kOpcodeHandlers[opcode];
  }

  void EndControl() {
    DCHECK(!control_.empty());
    Control* current = &control_.back();
    stack_.shrink_to(current->stack_depth);
    current->reachability = kUnreachable;
    current_code_reachable_and_ok_ = false;
  }

  template <typename func>
  V8_INLINE void InitMerge(Merge<Value>* merge, uint32_t arity, func get_val) {
    merge->arity = arity;
    if constexpr (std::is_null_pointer_v<func>) {
      DCHECK_EQ(0, arity);
    } else if (arity == 1) {
      merge->vals.first = get_val(0);
    } else if (arity > 1) {
      merge->vals.array = this->zone()->template AllocateArray<Value>(arity);
      for (uint32_t i = 0; i < arity; i++) {
        merge->vals.array[i] = get_val(i);
      }
    }
  }

  // In reachable code, check if there are at least {count} values on the stack.
  // In unreachable code, if there are less than {count} values on the stack,
  // insert a number of unreachable values underneath the current values equal
  // to the difference, and return that number.
  V8_INLINE int EnsureStackArguments(int count) {
    uint32_t limit = control_.back().stack_depth;
    if (V8_LIKELY(stack_.size() >= count + limit)) return 0;
    return EnsureStackArguments_Slow(count);
  }

  V8_NOINLINE V8_PRESERVE_MOST int EnsureStackArguments_Slow(int count) {
    uint32_t limit = control_.back().stack_depth;
    if (!VALIDATE(control_.back().unreachable())) {
      NotEnoughArgumentsError(count, stack_.size() - limit);
    }
    // Silently create unreachable values out of thin air underneath the
    // existing stack values. To do so, we have to move existing stack values
    // upwards in the stack, then instantiate the new Values as
    // {UnreachableValue}.
    int current_values = stack_.size() - limit;
    int additional_values = count - current_values;
    DCHECK_GT(additional_values, 0);
    // Ensure that after this operation there is still room for one more value.
    // Callers might not expect this operation to push values on the stack
    // (because it only does so in exceptional cases).
    stack_.EnsureMoreCapacity(additional_values + 1, this->zone_);
    Value unreachable_value = UnreachableValue(this->pc_);
    for (int i = 0; i < additional_values; ++i) stack_.push(unreachable_value);
    if (current_values > 0) {
      // Move the current values up to the end of the stack, and create
      // unreachable values below.
      Value* stack_base = stack_value(current_values + additional_values);
      for (int i = current_values - 1; i >= 0; i--) {
        stack_base[additional_values + i] = stack_base[i];
      }
      for (int i = 0; i < additional_values; i++) {
        stack_base[i] = UnreachableValue(this->pc_);
      }
    }
    return additional_values;
  }

  V8_INLINE void ValidateParameters(const FunctionSig* sig) {
    int num_params = static_cast<int>(sig->parameter_count());
    EnsureStackArguments(num_params);
    Value* param_base = stack_.end() - num_params;
    for (int i = 0; i < num_params; i++) {
      ValidateStackValue(i, param_base[i], sig->GetParam(i));
    }
  }

  // Drops a number of stack elements equal to the {sig}'s parameter count (0 if
  // {sig} is null), or all of them if less are present.
  V8_INLINE void DropArgs(const FunctionSig* sig) {
    int count = static_cast<int>(sig->parameter_count());
    Drop(count);
  }

  V8_INLINE PoppedArgVector PopArgs(const StructType* type) {
    int count = static_cast<int>(type->field_count());
    EnsureStackArguments(count);
    DCHECK_LE(control_.back().stack_depth, stack_size());
    DCHECK_GE(stack_size() - control_.back().stack_depth, count);
    Value* args_base = stack_.end() - count;
    for (int i = 0; i < count; i++) {
      ValidateStackValue(i, args_base[i], type->field(i).Unpacked());
    }
    // Note: Popping from the {FastZoneVector} does not invalidate the old (now
    // out-of-range) elements.
    stack_.pop(count);
    return PoppedArgVector{base::VectorOf(args_base, count)};
  }
  // Drops a number of stack elements equal to the struct's field count, or all
  // of them if less are present.
  V8_INLINE void DropArgs(const StructType* type) {
    Drop(static_cast<int>(type->field_count()));
  }

  // Pops arguments as required by signature, returning them by copy as a
  // vector.
  V8_INLINE PoppedArgVector PopArgs(const FunctionSig* sig) {
    int count = static_cast<int>(sig->parameter_count());
    EnsureStackArguments(count);
    DCHECK_LE(control_.back().stack_depth, stack_size());
    DCHECK_GE(stack_size() - control_.back().stack_depth, count);
    Value* args_base = stack_.end() - count;
    for (int i = 0; i < count; ++i) {
      ValidateStackValue(i, args_base[i], sig->GetParam(i));
    }
    // Note: Popping from the {FastZoneVector} does not invalidate the old (now
    // out-of-range) elements.
    stack_.pop(count);
    return PoppedArgVector{base::VectorOf(args_base, count)};
  }

  Control* PushControl(ControlKind kind, const BlockTypeImmediate& imm) {
    DCHECK(!control_.empty());
    ValidateParameters(&imm.sig);
    uint32_t consumed_values = static_cast<uint32_t>(imm.sig.parameter_count());
    uint32_t stack_depth = stack_.size();
    DCHECK_LE(consumed_values, stack_depth);
    uint32_t inner_stack_depth = stack_depth - consumed_values;
    DCHECK_LE(control_.back().stack_depth, inner_stack_depth);

    uint32_t init_stack_depth = this->locals_initialization_stack_depth();
    Reachability reachability = control_.back().innerReachability();
    control_.EnsureMoreCapacity(1, this->zone_);
    control_.emplace_back(this->zone_, kind, inner_stack_depth,
                          init_stack_depth, this->pc_, reachability);
    Control* new_block = &control_.back();

    Value* arg_base = stack_.end() - consumed_values;
    // Update the type of input nodes to the more general types expected by the
    // block. In particular, in unreachable code, the input would have bottom
    // type otherwise.
    for (uint32_t i = 0; i < consumed_values; ++i) {
      DCHECK_IMPLIES(this->ok(), IsSubtypeOf(arg_base[i].type, imm.in_type(i),
                                             this->module_) ||
                                     arg_base[i].type == kWasmBottom);
      arg_base[i].type = imm.in_type(i);
    }

    // Initialize start- and end-merges of {c} with values according to the
    // in- and out-types of {c} respectively.
    const uint8_t* pc = this->pc_;
    InitMerge(&new_block->end_merge, imm.out_arity(), [pc, &imm](uint32_t i) {
      return Value{pc, imm.out_type(i)};
    });
    InitMerge(&new_block->start_merge, imm.in_arity(),
              [arg_base](uint32_t i) { return arg_base[i]; });
    return new_block;
  }

  void PopControl() {
    // This cannot be the outermost control block.
    DCHECK_LT(1, control_.size());
    Control* c = &control_.back();
    DCHECK_LE(c->stack_depth, stack_.size());

    CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE(PopControl, c);

    // - In non-unreachable code, a loop just leaves the values on the stack.
    // - In unreachable code, it is not guaranteed that we have Values of the
    //   correct types on the stack, so we have to make sure we do. Their values
    //   do not matter, so we might as well push the (uninitialized) values of
    //   the loop's end merge.
    if (!c->is_loop() || c->unreachable()) {
      PushMergeValues(c, &c->end_merge);
    }
    RollbackLocalsInitialization(c);

    bool parent_reached =
        c->reachable() || c->end_merge.reached || c->is_onearmed_if();
    control_.pop();
    // If the parent block was reachable before, but the popped control does not
    // return to here, this block becomes "spec only reachable".
    if (!parent_reached) SetSucceedingCodeDynamicallyUnreachable();
    current_code_reachable_and_ok_ =
        VALIDATE(this->ok()) && control_.back().reachable();
  }

  int DecodeLoadMem(LoadType type, int prefix_len = 1) {
    MemoryAccessImmediate imm =
        MakeMemoryAccessImmediate(prefix_len, type.size_log_2());
    if (!this->Validate(this->pc_ + prefix_len, imm)) return 0;
    ValueType index_type = MemoryIndexType(imm.memory);
    Value index = Pop(index_type);
    Value* result = Push(type.value_type());
    if (V8_LIKELY(
            !CheckStaticallyOutOfBounds(imm.memory, type.size(), imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(LoadMem, type, imm, index, result);
    }
    return prefix_len + imm.length;
  }

  int DecodeLoadTransformMem(LoadType type, LoadTransformationKind transform,
                             uint32_t opcode_length) {
    // Load extends always load 64-bits.
    uint32_t max_alignment =
        transform == LoadTransformationKind::kExtend ? 3 : type.size_log_2();
    MemoryAccessImmediate imm =
        MakeMemoryAccessImmediate(opcode_length, max_alignment);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
    ValueType index_type = MemoryIndexType(imm.memory);
    Value index = Pop(index_type);
    Value* result = Push(kWasmS128);
    uintptr_t op_size =
        transform == LoadTransformationKind::kExtend ? 8 : type.size();
    if (V8_LIKELY(
            !CheckStaticallyOutOfBounds(imm.memory, op_size, imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(LoadTransform, type, transform, imm,
                                         index, result);
    }
    return opcode_length + imm.length;
  }

  int DecodeLoadLane(WasmOpcode opcode, LoadType type, uint32_t opcode_length) {
    MemoryAccessImmediate mem_imm =
        MakeMemoryAccessImmediate(opcode_length, type.size_log_2());
    if (!this->Validate(this->pc_ + opcode_length, mem_imm)) return 0;
    SimdLaneImmediate lane_imm(this, this->pc_ + opcode_length + mem_imm.length,
                               validate);
    if (!this->Validate(this->pc_ + opcode_length, opcode, lane_imm)) return 0;
    ValueType index_type = MemoryIndexType(mem_imm.memory);
    auto [index, v128] = Pop(index_type, kWasmS128);

    Value* result = Push(kWasmS128);
    if (V8_LIKELY(!CheckStaticallyOutOfBounds(mem_imm.memory, type.size(),
                                              mem_imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(LoadLane, type, v128, index, mem_imm,
                                         lane_imm.lane, result);
    }
    return opcode_length + mem_imm.length + lane_imm.length;
  }

  int DecodeStoreLane(WasmOpcode opcode, StoreType type,
                      uint32_t opcode_length) {
    MemoryAccessImmediate mem_imm =
        MakeMemoryAccessImmediate(opcode_length, type.size_log_2());
    if (!this->Validate(this->pc_ + opcode_length, mem_imm)) return 0;
    SimdLaneImmediate lane_imm(this, this->pc_ + opcode_length + mem_imm.length,
                               validate);
    if (!this->Validate(this->pc_ + opcode_length, opcode, lane_imm)) return 0;
    ValueType index_type = MemoryIndexType(mem_imm.memory);
    auto [index, v128] = Pop(index_type, kWasmS128);

    if (V8_LIKELY(!CheckStaticallyOutOfBounds(mem_imm.memory, type.size(),
                                              mem_imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(StoreLane, type, mem_imm, index, v128,
                                         lane_imm.lane);
    }
    return opcode_length + mem_imm.length + lane_imm.length;
  }

  bool CheckStaticallyOutOfBounds(const WasmMemory* memory, uint64_t size,
                                  uint64_t offset) {
    const bool statically_oob =
        !base::IsInBounds<uint64_t>(offset, size, memory->max_memory_size);
    if (V8_UNLIKELY(statically_oob)) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(Trap, TrapReason::kTrapMemOutOfBounds);
      SetSucceedingCodeDynamicallyUnreachable();
    }
    return statically_oob;
  }

  int DecodeStoreMem(StoreType store, int prefix_len = 1) {
    MemoryAccessImmediate imm =
        MakeMemoryAccessImmediate(prefix_len, store.size_log_2());
    if (!this->Validate(this->pc_ + prefix_len, imm)) return 0;
    ValueType index_type = MemoryIndexType(imm.memory);
    auto [index, value] = Pop(index_type, store.value_type());
    if (V8_LIKELY(!CheckStaticallyOutOfBounds(imm.memory, store.size(),
                                              imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(StoreMem, store, imm, index, value);
    }
    return prefix_len + imm.length;
  }

  uint32_t SimdConstOp(uint32_t opcode_length) {
    Simd128Immediate imm(this, this->pc_ + opcode_length, validate);
    Value* result = Push(kWasmS128);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(S128Const, imm, result);
    return opcode_length + kSimd128Size;
  }

  uint32_t SimdExtractLane(WasmOpcode opcode, ValueType type,
                           uint32_t opcode_length) {
    SimdLaneImmediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, opcode, imm)) return 0;
    Value input = Pop(kWasmS128);
    Value* result = Push(type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(SimdLaneOp, opcode, imm,
                                       base::VectorOf({input}), result);
    return opcode_length + imm.length;
  }

  uint32_t SimdReplaceLane(WasmOpcode opcode, ValueType type,
                           uint32_t opcode_length) {
    SimdLaneImmediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, opcode, imm)) return 0;
    auto [v128, lane_val] = Pop(kWasmS128, type);
    Value* result = Push(kWasmS128);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(
        SimdLaneOp, opcode, imm, base::VectorOf({v128, lane_val}), result);
    return opcode_length + imm.length;
  }

  uint32_t Simd8x16ShuffleOp(uint32_t opcode_length) {
    Simd128Immediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
    auto [input0, input1] = Pop(kWasmS128, kWasmS128);
    Value* result = Push(kWasmS128);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(Simd8x16ShuffleOp, imm, input0, input1,
                                       result);
    return opcode_length + 16;
  }

  uint32_t DecodeSimdOpcode(WasmOpcode opcode, uint32_t opcode_length) {
    if constexpr (decoding_mode == kConstantExpression) {
      // Currently, only s128.const is allowed in constant expressions.
      if (opcode != kExprS128Const) {
        this->DecodeError("opcode %s is not allowed in constant expressions",
                          this->SafeOpcodeNameAt(this->pc()));
        return 0;
      }
      return SimdConstOp(opcode_length);
    }
    // opcode_length is the number of bytes that this SIMD-specific opcode takes
    // up in the LEB128 encoded form.
    switch (opcode) {
      case kExprF64x2ExtractLane:
        return SimdExtractLane(opcode, kWasmF64, opcode_length);
      case kExprF16x8ExtractLane: {
        if (!v8_flags.experimental_wasm_fp16) {
          this->DecodeError(
              "invalid simd opcode: 0x%x, "
              "enable with --experimental-wasm-fp16",
              opcode);
          return 0;
        }
        [[fallthrough]];
      }
      case kExprF32x4ExtractLane:
        return SimdExtractLane(opcode, kWasmF32, opcode_length);
      case kExprI64x2ExtractLane:
        return SimdExtractLane(opcode, kWasmI64, opcode_length);
      case kExprI32x4ExtractLane:
      case kExprI16x8ExtractLaneS:
      case kExprI16x8ExtractLaneU:
      case kExprI8x16ExtractLaneS:
      case kExprI8x16ExtractLaneU:
        return SimdExtractLane(opcode, kWasmI32, opcode_length);
      case kExprF64x2ReplaceLane:
        return SimdReplaceLane(opcode, kWasmF64, opcode_length);
      case kExprF16x8ReplaceLane: {
        if (!v8_flags.experimental_wasm_fp16) {
          this->DecodeError(
              "invalid simd opcode: 0x%x, "
              "enable with --experimental-wasm-fp16",
              opcode);
          return 0;
        }
        [[fallthrough]];
      }
      case kExprF32x4ReplaceLane:
        return SimdReplaceLane(opcode, kWasmF32, opcode_length);
      case kExprI64x2ReplaceLane:
        return SimdReplaceLane(opcode, kWasmI64, opcode_length);
      case kExprI32x4ReplaceLane:
      case kExprI16x8ReplaceLane:
      case kExprI8x16ReplaceLane:
        return SimdReplaceLane(opcode, kWasmI32, opcode_length);
      case kExprI8x16Shuffle:
        return Simd8x16ShuffleOp(opcode_length);
      case kExprS128LoadMem:
        return DecodeLoadMem(LoadType::kS128Load, opcode_length);
      case kExprS128StoreMem:
        return DecodeStoreMem(StoreType::kS128Store, opcode_length);
      case kExprS128Load32Zero:
        return DecodeLoadTransformMem(LoadType::kI32Load,
                                      LoadTransformationKind::kZeroExtend,
                                      opcode_length);
      case kExprS128Load64Zero:
        return DecodeLoadTransformMem(LoadType::kI64Load,
                                      LoadTransformationKind::kZeroExtend,
                                      opcode_length);
      case kExprS128Load8Splat:
        return DecodeLoadTransformMem(LoadType::kI32Load8S,
                                      LoadTransformationKind::kSplat,
                                      opcode_length);
      case kExprS128Load16Splat:
        return DecodeLoadTransformMem(LoadType::kI32Load16S,
                                      LoadTransformationKind::kSplat,
                                      opcode_length);
      case kExprS128Load32Splat:
        return DecodeLoadTransformMem(
            LoadType::kI32Load, LoadTransformationKind::kSplat, opcode_length);
      case kExprS128Load64Splat:
        return DecodeLoadTransformMem(
            LoadType::kI64Load, LoadTransformationKind::kSplat, opcode_length);
      case kExprS128Load8x8S:
        return DecodeLoadTransformMem(LoadType::kI32Load8S,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load8x8U:
        return DecodeLoadTransformMem(LoadType::kI32Load8U,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load16x4S:
        return DecodeLoadTransformMem(LoadType::kI32Load16S,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load16x4U:
        return DecodeLoadTransformMem(LoadType::kI32Load16U,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load32x2S:
        return DecodeLoadTransformMem(LoadType::kI64Load32S,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load32x2U:
        return DecodeLoadTransformMem(LoadType::kI64Load32U,
                                      LoadTransformationKind::kExtend,
                                      opcode_length);
      case kExprS128Load8Lane: {
        return DecodeLoadLane(opcode, LoadType::kI32Load8S, opcode_length);
      }
      case kExprS128Load16Lane: {
        return DecodeLoadLane(opcode, LoadType::kI32Load16S, opcode_length);
      }
      case kExprS128Load32Lane: {
        return DecodeLoadLane(opcode, LoadType::kI32Load, opcode_length);
      }
      case kExprS128Load64Lane: {
        return DecodeLoadLane(opcode, LoadType::kI64Load, opcode_length);
      }
      case kExprS128Store8Lane: {
        return DecodeStoreLane(opcode, StoreType::kI32Store8, opcode_length);
      }
      case kExprS128Store16Lane: {
        return DecodeStoreLane(opcode, StoreType::kI32Store16, opcode_length);
      }
      case kExprS128Store32Lane: {
        return DecodeStoreLane(opcode, StoreType::kI32Store, opcode_length);
      }
      case kExprS128Store64Lane: {
        return DecodeStoreLane(opcode, StoreType::kI64Store, opcode_length);
      }
      case kExprS128Const:
        return SimdConstOp(opcode_length);
      case kExprF16x8Splat:
      case kExprF16x8Abs:
      case kExprF16x8Neg:
      case kExprF16x8Sqrt:
      case kExprF16x8Ceil:
      case kExprF16x8Floor:
      case kExprF16x8Trunc:
      case kExprF16x8NearestInt:
      case kExprF16x8Eq:
      case kExprF16x8Ne:
      case kExprF16x8Lt:
      case kExprF16x8Gt:
      case kExprF16x8Le:
      case kExprF16x8Ge:
      case kExprF16x8Add:
      case kExprF16x8Sub:
      case kExprF16x8Mul:
      case kExprF16x8Div:
      case kExprF16x8Min:
      case kExprF16x8Max:
      case kExprF16x8Pmin:
      case kExprF16x8Pmax:
      case kExprI16x8SConvertF16x8:
      case kExprI16x8UConvertF16x8:
      case kExprF16x8SConvertI16x8:
      case kExprF16x8UConvertI16x8:
      case kExprF16x8DemoteF32x4Zero:
      case kExprF16x8DemoteF64x2Zero:
      case kExprF32x4PromoteLowF16x8:
      case kExprF16x8Qfma:
      case kExprF16x8Qfms: {
        if (!v8_flags.experimental_wasm_fp16) {
          this->DecodeError(
              "invalid simd opcode: 0x%x, "
              "enable with --experimental-wasm-fp16",
              opcode);
          return 0;
        }
        [[fallthrough]];
      }
      default: {
        const FunctionSig* sig = WasmOpcodes::Signature(opcode);
        if (!VALIDATE(sig != nullptr)) {
          this->DecodeError("invalid simd opcode");
          return 0;
        }
        PoppedArgVector args = PopArgs(sig);
        Value* results = sig->return_count() == 0 ? nullptr : PushReturns(sig);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(SimdOp, opcode, args.data(),
                                           results);
        return opcode_length;
      }
    }
  }

  // Returns true if type checking will always fail, either because the types
  // are unrelated or because the target_type is one of the null sentinels and
  // conversion to null does not succeed.
  bool TypeCheckAlwaysFails(Value obj, HeapType expected_type,
                            bool null_succeeds) {
    bool types_unrelated =
        !IsSubtypeOf(ValueType::Ref(expected_type), obj.type, this->module_) &&
        !IsSubtypeOf(obj.type, ValueType::RefNull(expected_type),
                     this->module_);
    // For "unrelated" types the check can still succeed for the null value on
    // instructions treating null as a successful check.
    // TODO(12868): For string views, this implementation anticipates that
    // https://github.com/WebAssembly/stringref/issues/40 will be resolved
    // by making the views standalone types.
    return (types_unrelated &&
            (!null_succeeds || !obj.type.is_nullable() ||
             obj.type.is_string_view() || expected_type.is_string_view())) ||
           ((!null_succeeds || !obj.type.is_nullable()) &&
            (expected_type.representation() == HeapType::kNone ||
             expected_type.representation() == HeapType::kNoFunc ||
             expected_type.representation() == HeapType::kNoExtern ||
             expected_type.representation() == HeapType::kNoExn));
  }

  // Checks if {obj} is a subtype of type, thus checking will always
  // succeed.
  bool TypeCheckAlwaysSucceeds(Value obj, HeapType type) {
    return IsSubtypeOf(obj.type, ValueType::RefNull(type), this->module_);
  }

#define NON_CONST_ONLY                                                    \
  if constexpr (decoding_mode == kConstantExpression) {                   \
    this->DecodeError("opcode %s is not allowed in constant expressions", \
                      this->SafeOpcodeNameAt(this->pc()));                \
    return 0;                                                             \
  }

  int DecodeGCOpcode(WasmOpcode opcode, uint32_t opcode_length) {
    // Bigger GC opcodes are handled via {DecodeStringRefOpcode}, so we can
    // assume here that opcodes are within [0xfb00, 0xfbff].
    // This assumption might help the big switch below.
    V8_ASSUME(opcode >> 8 == kGCPrefix);
    switch (opcode) {
      case kExprStructNew: {
        StructIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        PoppedArgVector args = PopArgs(imm.struct_type);
        Value* value = Push(ValueType::Ref(imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StructNew, imm, args.data(), value);
        return opcode_length + imm.length;
      }
      case kExprStructNewDefault: {
        StructIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        if (ValidationTag::validate) {
          for (uint32_t i = 0; i < imm.struct_type->field_count(); i++) {
            ValueType ftype = imm.struct_type->field(i);
            if (!VALIDATE(ftype.is_defaultable())) {
              this->DecodeError(
                  "%s: struct type %d has field %d of non-defaultable type %s",
                  WasmOpcodes::OpcodeName(opcode), imm.index, i,
                  ftype.name().c_str());
              return 0;
            }
          }
        }
        Value* value = Push(ValueType::Ref(imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StructNewDefault, imm, value);
        return opcode_length + imm.length;
      }
      case kExprStructGet: {
        NON_CONST_ONLY
        FieldImmediate field(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, field)) return 0;
        ValueType field_type =
            field.struct_imm.struct_type->field(field.field_imm.index);
        if (!VALIDATE(!field_type.is_packed())) {
          this->DecodeError(
              "struct.get: Immediate field %d of type %d has packed type %s. "
              "Use struct.get_s or struct.get_u instead.",
              field.field_imm.index, field.struct_imm.index,
              field_type.name().c_str());
          return 0;
        }
        Value struct_obj = Pop(ValueType::RefNull(field.struct_imm.index));
        Value* value = Push(field_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StructGet, struct_obj, field, true,
                                           value);
        return opcode_length + field.length;
      }
      case kExprStructGetU:
      case kExprStructGetS: {
        NON_CONST_ONLY
        FieldImmediate field(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, field)) return 0;
        ValueType field_type =
            field.struct_imm.struct_type->field(field.field_imm.index);
        if (!VALIDATE(field_type.is_packed())) {
          this->DecodeError(
              "%s: Immediate field %d of type %d has non-packed type %s. Use "
              "struct.get instead.",
              WasmOpcodes::OpcodeName(opcode), field.field_imm.index,
              field.struct_imm.index, field_type.name().c_str());
          return 0;
        }
        Value struct_obj = Pop(ValueType::RefNull(field.struct_imm.index));
        Value* value = Push(field_type.Unpacked());
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StructGet, struct_obj, field,
                                           opcode == kExprStructGetS, value);
        return opcode_length + field.length;
      }
      case kExprStructSet: {
        NON_CONST_ONLY
        FieldImmediate field(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, field)) return 0;
        const StructType* struct_type = field.struct_imm.struct_type;
        if (!VALIDATE(struct_type->mutability(field.field_imm.index))) {
          this->DecodeError("struct.set: Field %d of type %d is immutable.",
                            field.field_imm.index, field.struct_imm.index);
          return 0;
        }
        auto [struct_obj, field_value] =
            Pop(ValueType::RefNull(field.struct_imm.index),
                struct_type->field(field.field_imm.index).Unpacked());
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StructSet, struct_obj, field,
                                           field_value);
        return opcode_length + field.length;
      }
      case kExprArrayNew: {
        ArrayIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        auto [initial_value, length] =
            Pop(imm.array_type->element_type().Unpacked(), kWasmI32);
        Value* value = Push(ValueType::Ref(imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayNew, imm, length, initial_value,
                                           value);
        return opcode_length + imm.length;
      }
      case kExprArrayNewDefault: {
        ArrayIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        if (!VALIDATE(imm.array_type->element_type().is_defaultable())) {
          this->DecodeError(
              "%s: array type %d has non-defaultable element type %s",
              WasmOpcodes::OpcodeName(opcode), imm.index,
              imm.array_type->element_type().name().c_str());
          return 0;
        }
        Value length = Pop(kWasmI32);
        Value* value = Push(ValueType::Ref(imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayNewDefault, imm, length, value);
        return opcode_length + imm.length;
      }
      case kExprArrayNewData: {
        // TODO(14616): Add check that array sharedness == segment sharedness?
        NON_CONST_ONLY
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        ValueType element_type = array_imm.array_type->element_type();
        if (element_type.is_reference()) {
          this->DecodeError(
              "array.new_data can only be used with numeric-type arrays, found "
              "array type #%d instead",
              array_imm.index);
          return 0;
        }
        const uint8_t* data_index_pc =
            this->pc_ + opcode_length + array_imm.length;
        IndexImmediate data_segment(this, data_index_pc, "data segment",
                                    validate);
        if (!this->ValidateDataSegment(data_index_pc, data_segment)) return 0;

        auto [offset, length] = Pop(kWasmI32, kWasmI32);

        Value* array = Push(ValueType::Ref(array_imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayNewSegment, array_imm,
                                           data_segment, offset, length, array);
        return opcode_length + array_imm.length + data_segment.length;
      }
      case kExprArrayNewElem: {
        // TODO(14616): Add check that array sharedness == segment sharedness?
        NON_CONST_ONLY
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        ValueType element_type = array_imm.array_type->element_type();
        if (element_type.is_numeric()) {
          this->DecodeError(
              "array.new_elem can only be used with reference-type arrays, "
              "found array type #%d instead",
              array_imm.index);
          return 0;
        }
        const uint8_t* elem_index_pc =
            this->pc_ + opcode_length + array_imm.length;
        IndexImmediate elem_segment(this, elem_index_pc, "element segment",
                                    validate);
        if (!this->ValidateElementSegment(elem_index_pc, elem_segment)) {
          return 0;
        }

        ValueType elem_segment_type =
            this->module_->elem_segments[elem_segment.index].type;
        if (V8_UNLIKELY(
                !IsSubtypeOf(elem_segment_type, element_type, this->module_))) {
          this->DecodeError(
              "array.new_elem: segment type %s is not a subtype of array "
              "element type %s",
              elem_segment_type.name().c_str(), element_type.name().c_str());
          return 0;
        }

        auto [offset, length] = Pop(kWasmI32, kWasmI32);
        Value* array = Push(ValueType::Ref(array_imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayNewSegment, array_imm,
                                           elem_segment, offset, length, array);
        return opcode_length + array_imm.length + elem_segment.length;
      }
      case kExprArrayInitData: {
        NON_CONST_ONLY
        // TODO(14616): Add check that array sharedness == segment sharedness?
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        if (!array_imm.array_type->mutability()) {
          this->DecodeError(
              "array.init_data can only be used with mutable arrays, found "
              "array type #%d instead",
              array_imm.index);
          return 0;
        }
        ValueType element_type = array_imm.array_type->element_type();
        if (element_type.is_reference()) {
          this->DecodeError(
              "array.init_data can only be used with numeric-type arrays, "
              "found array type #%d instead",
              array_imm.index);
          return 0;
        }
        const uint8_t* data_index_pc =
            this->pc_ + opcode_length + array_imm.length;
        IndexImmediate data_segment(this, data_index_pc, "data segment",
                                    validate);
        if (!this->ValidateDataSegment(data_index_pc, data_segment)) return 0;

        auto [array, array_index, data_offset, length] = Pop(
            ValueType::RefNull(array_imm.index), kWasmI32, kWasmI32, kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayInitSegment, array_imm,
                                           data_segment, array, array_index,
                                           data_offset, length);
        return opcode_length + array_imm.length + data_segment.length;
      }
      case kExprArrayInitElem: {
        NON_CONST_ONLY
        // TODO(14616): Add check that array sharedness == segment sharedness?
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        if (!array_imm.array_type->mutability()) {
          this->DecodeError(
              "array.init_elem can only be used with mutable arrays, found "
              "array type #%d instead",
              array_imm.index);
          return 0;
        }
        ValueType element_type = array_imm.array_type->element_type();
        if (element_type.is_numeric()) {
          this->DecodeError(
              "array.init_elem can only be used with reference-type arrays, "
              "found array type #%d instead",
              array_imm.index);
          return 0;
        }
        const uint8_t* elem_index_pc =
            this->pc_ + opcode_length + array_imm.length;
        IndexImmediate elem_segment(this, elem_index_pc, "element segment",
                                    validate);
        if (!this->ValidateElementSegment(elem_index_pc, elem_segment)) {
          return 0;
        }
        ValueType segment_type =
            this->module_->elem_segments[elem_segment.index].type;
        if (!VALIDATE(IsSubtypeOf(segment_type, element_type, this->module_))) {
          this->DecodeError(
              "array.init_elem: segment type %s is not a subtype of array "
              "element type %s",
              segment_type.name().c_str(), element_type.name().c_str());
          return 0;
        }

        auto [array, array_index, elem_offset, length] = Pop(
            ValueType::RefNull(array_imm.index), kWasmI32, kWasmI32, kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayInitSegment, array_imm,
                                           elem_segment, array, array_index,
                                           elem_offset, length);
        return opcode_length + array_imm.length + elem_segment.length;
      }
      case kExprArrayGetS:
      case kExprArrayGetU: {
        NON_CONST_ONLY
        ArrayIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        if (!VALIDATE(imm.array_type->element_type().is_packed())) {
          this->DecodeError(
              "%s: Immediate array type %d has non-packed type %s. Use "
              "array.get instead.",
              WasmOpcodes::OpcodeName(opcode), imm.index,
              imm.array_type->element_type().name().c_str());
          return 0;
        }
        auto [array_obj, index] = Pop(ValueType::RefNull(imm.index), kWasmI32);
        Value* value = Push(imm.array_type->element_type().Unpacked());
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayGet, array_obj, imm, index,
                                           opcode == kExprArrayGetS, value);
        return opcode_length + imm.length;
      }
      case kExprArrayGet: {
        NON_CONST_ONLY
        ArrayIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        if (!VALIDATE(!imm.array_type->element_type().is_packed())) {
          this->DecodeError(
              "array.get: Immediate array type %d has packed type %s. Use "
              "array.get_s or array.get_u instead.",
              imm.index, imm.array_type->element_type().name().c_str());
          return 0;
        }
        auto [array_obj, index] = Pop(ValueType::RefNull(imm.index), kWasmI32);
        Value* value = Push(imm.array_type->element_type());
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayGet, array_obj, imm, index,
                                           true, value);
        return opcode_length + imm.length;
      }
      case kExprArraySet: {
        NON_CONST_ONLY
        ArrayIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        if (!VALIDATE(imm.array_type->mutability())) {
          this->DecodeError("array.set: immediate array type %d is immutable",
                            imm.index);
          return 0;
        }
        auto [array_obj, index, value] =
            Pop(ValueType::RefNull(imm.index), kWasmI32,
                imm.array_type->element_type().Unpacked());
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArraySet, array_obj, imm, index,
                                           value);
        return opcode_length + imm.length;
      }
      case kExprArrayLen: {
        NON_CONST_ONLY
        Value array_obj = Pop(kWasmArrayRef);
        Value* value = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayLen, array_obj, value);
        return opcode_length;
      }
      case kExprArrayCopy: {
        NON_CONST_ONLY
        ArrayIndexImmediate dst_imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, dst_imm)) return 0;
        if (!VALIDATE(dst_imm.array_type->mutability())) {
          this->DecodeError(
              "array.copy: immediate destination array type #%d is immutable",
              dst_imm.index);
          return 0;
        }
        ArrayIndexImmediate src_imm(
            this, this->pc_ + opcode_length + dst_imm.length, validate);
        if (!this->Validate(this->pc_ + opcode_length + dst_imm.length,
                            src_imm)) {
          return 0;
        }
        if (!IsSubtypeOf(src_imm.array_type->element_type(),
                         dst_imm.array_type->element_type(), this->module_)) {
          this->DecodeError(
              "array.copy: source array's #%d element type is not a subtype of "
              "destination array's #%d element type",
              src_imm.index, dst_imm.index);
          return 0;
        }
        auto [dst, dst_index, src, src_index, length] =
            Pop(ValueType::RefNull(dst_imm.index), kWasmI32,
                ValueType::RefNull(src_imm.index), kWasmI32, kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayCopy, dst, dst_index, src,
                                           src_index, src_imm, length);
        return opcode_length + dst_imm.length + src_imm.length;
      }
      case kExprArrayFill: {
        NON_CONST_ONLY
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        if (!VALIDATE(array_imm.array_type->mutability())) {
          this->DecodeError("array.init: immediate array type #%d is immutable",
                            array_imm.index);
          return 0;
        }

        auto [array, offset, value, length] =
            Pop(ValueType::RefNull(array_imm.index), kWasmI32,
                array_imm.array_type->element_type().Unpacked(), kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayFill, array_imm, array, offset,
                                           value, length);
        return opcode_length + array_imm.length;
      }
      case kExprArrayNewFixed: {
        ArrayIndexImmediate array_imm(this, this->pc_ + opcode_length,
                                      validate);
        if (!this->Validate(this->pc_ + opcode_length, array_imm)) return 0;
        IndexImmediate length_imm(this,
                                  this->pc_ + opcode_length + array_imm.length,
                                  "array.new_fixed length", validate);
        uint32_t elem_count = length_imm.index;
        if (!VALIDATE(elem_count <= kV8MaxWasmArrayNewFixedLength)) {
          this->DecodeError(
              "Requested length %u for array.new_fixed too large, maximum is "
              "%zu",
              length_imm.index, kV8MaxWasmArrayNewFixedLength);
          return 0;
        }
        ValueType element_type = array_imm.array_type->element_type();
        std::vector<ValueType> element_types(elem_count,
                                             element_type.Unpacked());
        FunctionSig element_sig(0, elem_count, element_types.data());
        PoppedArgVector elements = PopArgs(&element_sig);
        Value* result = Push(ValueType::Ref(array_imm.index));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ArrayNewFixed, array_imm, length_imm,
                                           elements.data(), result);
        return opcode_length + array_imm.length + length_imm.length;
      }
      case kExprRefI31: {
        Value input = Pop(kWasmI32);
        Value* value = Push(ValueType::Ref(HeapType::kI31));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(RefI31, input, value);
        return opcode_length;
      }
      case kExprI31GetS: {
        NON_CONST_ONLY
        Value i31 = Pop(kWasmI31Ref);
        Value* value = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(I31GetS, i31, value);
        return opcode_length;
      }
      case kExprI31GetU: {
        NON_CONST_ONLY
        Value i31 = Pop(kWasmI31Ref);
        Value* value = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(I31GetU, i31, value);
        return opcode_length;
      }
      case kExprRefCast:
      case kExprRefCastNull: {
        NON_CONST_ONLY
        HeapTypeImmediate imm(this->enabled_, this, this->pc_ + opcode_length,
                              validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        opcode_length += imm.length;

        Value obj = Pop();

        HeapType target_type = imm.type;

        if (!VALIDATE((obj.type.is_object_reference() &&
                       IsSameTypeHierarchy(obj.type.heap_type(), target_type,
                                           this->module_)) ||
                      obj.type.is_bottom())) {
          this->DecodeError(
              obj.pc(),
              "Invalid types for %s: %s of type %s has to "
              "be in the same reference type hierarchy as (ref %s)",
              WasmOpcodes::OpcodeName(opcode), SafeOpcodeNameAt(obj.pc()),
              obj.type.name().c_str(), target_type.name().c_str());
          return 0;
        }
        if (!VALIDATE(!target_type.is_string_view())) {
          // TODO(12868): This reflects the current state of discussion at
          // https://github.com/WebAssembly/stringref/issues/40
          // It is suboptimal because it allows classifying a stringview_wtf16
          // as a stringref. This would be solved by making the views types
          // that aren't subtypes of anyref, which is one of the possible
          // resolutions of that discussion.
          this->DecodeError(
              this->pc_,
              "Invalid type for %s: string views are not classifiable",
              WasmOpcodes::OpcodeName(opcode));
          return 0;
        }

        bool null_succeeds = opcode == kExprRefCastNull;
        Value* value = Push(ValueType::RefMaybeNull(
            target_type, null_succeeds ? kNullable : kNonNullable));
        if (current_code_reachable_and_ok_) {
          // This logic ensures that code generation can assume that functions
          // can only be cast to function types, and data objects to data types.
          if (V8_UNLIKELY(TypeCheckAlwaysSucceeds(obj, target_type))) {
            if (obj.type.is_nullable() && !null_succeeds) {
              CALL_INTERFACE(AssertNotNullTypecheck, obj, value);
            } else {
              CALL_INTERFACE(Forward, obj, value);
            }
          } else if (V8_UNLIKELY(TypeCheckAlwaysFails(obj, target_type,
                                                      null_succeeds))) {
            // Unrelated types. The only way this will not trap is if the object
            // is null.
            if (obj.type.is_nullable() && null_succeeds) {
              CALL_INTERFACE(AssertNullTypecheck, obj, value);
            } else {
              CALL_INTERFACE(Trap, TrapReason::kTrapIllegalCast);
              // We know that the following code is not reachable, but according
              // to the spec it technically is. Set it to spec-only reachable.
              SetSucceedingCodeDynamicallyUnreachable();
            }
          } else {
            if (target_type.is_index()) {
              CALL_INTERFACE(RefCast, target_type.ref_index(), obj, value,
                             null_succeeds);
            } else {
              CALL_INTERFACE(RefCastAbstract, obj, target_type, value,
                             null_succeeds);
            }
          }
        }
        return opcode_length;
      }
      case kExprRefTestNull:
      case kExprRefTest: {
        NON_CONST_ONLY
        HeapTypeImmediate imm(this->enabled_, this, this->pc_ + opcode_length,
                              validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        opcode_length += imm.length;

        Value obj = Pop();
        HeapType target_type = imm.type;
        Value* result = Push(kWasmI32);

        if (!VALIDATE((obj.type.is_object_reference() &&
                       IsSameTypeHierarchy(obj.type.heap_type(), target_type,
                                           this->module_)) ||
                      obj.type.is_bottom())) {
          this->DecodeError(
              obj.pc(),
              "Invalid types for %s: %s of type %s has to "
              "be in the same reference type hierarchy as (ref %s)",
              WasmOpcodes::OpcodeName(opcode), SafeOpcodeNameAt(obj.pc()),
              obj.type.name().c_str(), target_type.name().c_str());
          return 0;
        }
        if (!VALIDATE(!target_type.is_string_view())) {
          // TODO(12868): This reflects the current state of discussion at
          // https://github.com/WebAssembly/stringref/issues/40
          // It is suboptimal because it allows classifying a stringview_wtf16
          // as a stringref. This would be solved by making the views types
          // that aren't subtypes of anyref, which is one of the possible
          // resolutions of that discussion.
          this->DecodeError(
              this->pc_,
              "Invalid type for %s: string views are not classifiable",
              WasmOpcodes::OpcodeName(opcode));
          return 0;
        }
        bool null_succeeds = opcode == kExprRefTestNull;
        if (V8_LIKELY(current_code_reachable_and_ok_)) {
          // This logic ensures that code generation can assume that functions
          // can only be cast to function types, and data objects to data types.
          if (V8_UNLIKELY(TypeCheckAlwaysSucceeds(obj, target_type))) {
            // Type checking can still fail for null.
            if (obj.type.is_nullable() && !null_succeeds) {
              // We abuse ref.as_non_null, which isn't otherwise used as a unary
              // operator, as a sentinel for the negation of ref.is_null.
              CALL_INTERFACE(UnOp, kExprRefAsNonNull, obj, result);
            } else {
              CALL_INTERFACE(Drop);
              CALL_INTERFACE(I32Const, result, 1);
            }
          } else if (V8_UNLIKELY(TypeCheckAlwaysFails(obj, target_type,
                                                      null_succeeds))) {
            CALL_INTERFACE(Drop);
            CALL_INTERFACE(I32Const, result, 0);
          } else {
            if (imm.type.is_index()) {
              CALL_INTERFACE(RefTest, imm.type.ref_index(), obj, result,
                             null_succeeds);
            } else {
              CALL_INTERFACE(RefTestAbstract, obj, target_type, result,
                             null_succeeds);
            }
          }
        }
        return opcode_length;
      }
      case kExprRefCastNop: {
        NON_CONST_ONLY
        // Temporary non-standard instruction, for performance experiments.
        if (!VALIDATE(this->enabled_.has_ref_cast_nop())) {
          this->DecodeError(
              "Invalid opcode 0xfb4c (enable with "
              "--experimental-wasm-ref-cast-nop)");
          return 0;
        }
        HeapTypeImmediate imm(this->enabled_, this, this->pc_ + opcode_length,
                              validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        opcode_length += imm.length;
        HeapType target_type = imm.type;
        Value obj = Pop();
        if (!VALIDATE((obj.type.is_object_reference() &&
                       IsSameTypeHierarchy(obj.type.heap_type(), target_type,
                                           this->module_)) ||
                      obj.type.is_bottom())) {
          this->DecodeError(
              obj.pc(),
              "Invalid types for %s: %s of type %s has to "
              "be in the same reference type hierarchy as (ref %s)",
              WasmOpcodes::OpcodeName(opcode), SafeOpcodeNameAt(obj.pc()),
              obj.type.name().c_str(), target_type.name().c_str());
          return 0;
        }
        Value* value = Push(ValueType::Ref(target_type));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(Forward, obj, value);
        return opcode_length;
      }
      case kExprBrOnCast:
      case kExprBrOnCastFail: {
        NON_CONST_ONLY
        uint32_t pc_offset = opcode_length;
        BrOnCastImmediate flags_imm(this, this->pc_ + pc_offset, validate);
        pc_offset += flags_imm.length;
        return ParseBrOnCast(opcode, pc_offset, flags_imm.flags);
      }
      case kExprAnyConvertExtern: {
        Value extern_val = Pop(kWasmExternRef);
        ValueType intern_type = ValueType::RefMaybeNull(
            HeapType::kAny, Nullability(extern_val.type.is_nullable()));
        Value* intern_val = Push(intern_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(UnOp, kExprAnyConvertExtern,
                                           extern_val, intern_val);
        return opcode_length;
      }
      case kExprExternConvertAny: {
        Value val = Pop(kWasmAnyRef);
        ValueType extern_type = ValueType::RefMaybeNull(
            HeapType::kExtern, Nullability(val.type.is_nullable()));
        Value* extern_val = Push(extern_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(UnOp, kExprExternConvertAny, val,
                                           extern_val);
        return opcode_length;
      }
      default:
        this->DecodeError("invalid gc opcode: %x", opcode);
        return 0;
    }
  }

  enum class WasmArrayAccess { kRead, kWrite };

  int ParseBrOnCast(WasmOpcode opcode, uint32_t opcode_length,
                    BrOnCastFlags flags) {
    BranchDepthImmediate branch_depth(this, this->pc_ + opcode_length,
                                      validate);
    if (!this->Validate(this->pc_ + opcode_length, branch_depth,
                        control_.size())) {
      return 0;
    }
    uint32_t pc_offset = opcode_length + branch_depth.length;

    Value obj = Pop();

    HeapTypeImmediate src_imm(this->enabled_, this, this->pc_ + pc_offset,
                              validate);
    if (!this->Validate(this->pc_ + pc_offset, src_imm)) return 0;
    pc_offset += src_imm.length;
    ValueType src_type = ValueType::RefMaybeNull(
        src_imm.type, flags.src_is_null ? kNullable : kNonNullable);
    ValidateStackValue(0, obj, src_type);

    HeapTypeImmediate target_imm(this->enabled_, this, this->pc_ + pc_offset,
                                 validate);
    if (!this->Validate(this->pc_ + pc_offset, target_imm)) return 0;
    pc_offset += target_imm.length;
    bool null_succeeds = flags.res_is_null;
    ValueType target_type = ValueType::RefMaybeNull(
        target_imm.type, null_succeeds ? kNullable : kNonNullable);

    if (!VALIDATE(IsSubtypeOf(target_type, src_type, this->module_))) {
      this->DecodeError("invalid types for %s: %s is not a subtype of %s",
                        WasmOpcodes::OpcodeName(opcode),
                        target_type.name().c_str(), src_type.name().c_str());
      return 0;
    }

    if (!VALIDATE(
            (obj.type.is_object_reference() &&
             IsSameTypeHierarchy(obj.type.heap_type(), target_type.heap_type(),
                                 this->module_)) ||
            obj.type.is_bottom())) {
      this->DecodeError(obj.pc(),
                        "invalid types for %s: %s of type %s has to "
                        "be in the same reference type hierarchy as %s",
                        WasmOpcodes::OpcodeName(opcode),
                        SafeOpcodeNameAt(obj.pc()), obj.type.name().c_str(),
                        target_type.name().c_str());
      return 0;
    }

    Control* c = control_at(branch_depth.depth);
    if (c->br_merge()->arity == 0) {
      this->DecodeError("%s must target a branch of arity at least 1",
                        WasmOpcodes::OpcodeName(opcode));
      return 0;
    }

    if (opcode == kExprBrOnCast) {
      Value* value_on_branch = Push(target_type);
      if (!VALIDATE(
              (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kYes>(
                  c)))) {
        return 0;
      }
      if (V8_LIKELY(current_code_reachable_and_ok_)) {
        // This logic ensures that code generation can assume that functions
        // can only be cast to function types, and data objects to data types.
        if (V8_UNLIKELY(
                TypeCheckAlwaysSucceeds(obj, target_type.heap_type()))) {
          // The branch will still not be taken on null if not
          // {null_succeeds}.
          if (obj.type.is_nullable() && !null_succeeds) {
            CALL_INTERFACE(BrOnNonNull, obj, value_on_branch,
                           branch_depth.depth, false);
          } else {
            CALL_INTERFACE(Forward, obj, value_on_branch);
            CALL_INTERFACE(BrOrRet, branch_depth.depth);
            // We know that the following code is not reachable, but according
            // to the spec it technically is. Set it to spec-only reachable.
            SetSucceedingCodeDynamicallyUnreachable();
          }
          c->br_merge()->reached = true;
        } else if (V8_LIKELY(!TypeCheckAlwaysFails(obj, target_type.heap_type(),
                                                   null_succeeds))) {
          if (target_imm.type.is_index()) {
            CALL_INTERFACE(BrOnCast, target_imm.type.ref_index(), obj,
                           value_on_branch, branch_depth.depth, null_succeeds);
          } else {
            CALL_INTERFACE(BrOnCastAbstract, obj, target_type.heap_type(),
                           value_on_branch, branch_depth.depth, null_succeeds);
          }
          c->br_merge()->reached = true;
        }
      }

      Drop(*value_on_branch);
      Push(obj);  // Restore stack state on fallthrough.
      // The fallthrough type is the source type as specified in the br_on_cast
      // instruction. This can be a super type of the stack value. Furthermore
      // nullability gets refined to non-nullable if the cast target is
      // nullable, meaning the branch will be taken on null.
      DCHECK(!src_type.heap_type().is_bottom());
      bool fallthrough_nullable = flags.src_is_null && !flags.res_is_null;
      stack_value(1)->type = ValueType::RefMaybeNull(
          src_type.heap_type(),
          fallthrough_nullable ? kNullable : kNonNullable);
      CALL_INTERFACE_IF_OK_AND_REACHABLE(Forward, obj, stack_value(1));
      return pc_offset;

    } else {
      DCHECK_EQ(opcode, kExprBrOnCastFail);
      // The branch type is set based on the source type immediate (independent
      // of the actual stack value). If the target type is nullable, the branch
      // type is non-nullable.
      Push(flags.res_is_null ? src_type.AsNonNull() : src_type);
      CALL_INTERFACE_IF_OK_AND_REACHABLE(Forward, obj, stack_value(1));

      if (!VALIDATE(
              (TypeCheckBranch<PushBranchValues::kYes, RewriteStackTypes::kYes>(
                  c)))) {
        return 0;
      }

      Value result_on_fallthrough = CreateValue(target_type);
      if (V8_LIKELY(current_code_reachable_and_ok_)) {
        // This logic ensures that code generation can assume that functions
        // can only be cast between compatible types.
        if (V8_UNLIKELY(TypeCheckAlwaysFails(obj, target_type.heap_type(),
                                             null_succeeds))) {
          // The types are incompatible (i.e. neither of the two types is a
          // subtype of the other). Always branch.
          CALL_INTERFACE(Forward, obj, stack_value(1));
          CALL_INTERFACE(BrOrRet, branch_depth.depth);
          // We know that the following code is not reachable, but according
          // to the spec it technically is. Set it to spec-only reachable.
          SetSucceedingCodeDynamicallyUnreachable();
          c->br_merge()->reached = true;
        } else if (V8_UNLIKELY(
                       TypeCheckAlwaysSucceeds(obj, target_type.heap_type()))) {
          // The branch can still be taken on null.
          if (obj.type.is_nullable() && !null_succeeds) {
            CALL_INTERFACE(BrOnNull, obj, branch_depth.depth, true,
                           &result_on_fallthrough);
            c->br_merge()->reached = true;
          } else {
            // Otherwise, the type check always succeeds. Do not branch. Also,
            // make sure the object remains on the stack.
            result_on_fallthrough = obj;
          }
        } else {
          if (target_imm.type.is_index()) {
            CALL_INTERFACE(BrOnCastFail, target_imm.type.ref_index(), obj,
                           &result_on_fallthrough, branch_depth.depth,
                           null_succeeds);
          } else {
            CALL_INTERFACE(BrOnCastFailAbstract, obj, target_type.heap_type(),
                           &result_on_fallthrough, branch_depth.depth,
                           null_succeeds);
          }
          c->br_merge()->reached = true;
        }
      }
      // Make sure the correct value is on the stack state on fallthrough.
      Drop(obj);
      Push(result_on_fallthrough);
      return pc_offset;
    }
  }

  int DecodeStringNewWtf8(unibrow::Utf8Variant variant,
                          uint32_t opcode_length) {
    NON_CONST_ONLY
    bool null_on_invalid = variant == unibrow::Utf8Variant::kUtf8NoTrap;
    MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
    ValueType addr_type = MemoryIndexType(imm.memory);
    auto [offset, size] = Pop(addr_type, kWasmI32);
    Value* result = Push(ValueType::RefMaybeNull(
        HeapType::kString, null_on_invalid ? kNullable : kNonNullable));
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringNewWtf8, imm, variant, offset,
                                       size, result);
    return opcode_length + imm.length;
  }

  int DecodeStringMeasureWtf8(unibrow::Utf8Variant variant,
                              uint32_t opcode_length) {
    NON_CONST_ONLY
    Value str = Pop(kWasmStringRef);
    Value* result = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringMeasureWtf8, variant, str, result);
    return opcode_length;
  }

  int DecodeStringEncodeWtf8(unibrow::Utf8Variant variant,
                             uint32_t opcode_length) {
    NON_CONST_ONLY
    MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
    ValueType addr_type = MemoryIndexType(imm.memory);
    auto [str, addr] = Pop(kWasmStringRef, addr_type);
    Value* result = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringEncodeWtf8, imm, variant, str,
                                       addr, result);
    return opcode_length + imm.length;
  }

  int DecodeStringViewWtf8Encode(unibrow::Utf8Variant variant,
                                 uint32_t opcode_length) {
    NON_CONST_ONLY
    MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
    ValueType addr_type = MemoryIndexType(imm.memory);
    auto [view, addr, pos, bytes] =
        Pop(kWasmStringViewWtf8, addr_type, kWasmI32, kWasmI32);
    Value* next_pos = Push(kWasmI32);
    Value* bytes_out = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf8Encode, imm, variant, view,
                                       addr, pos, bytes, next_pos, bytes_out);
    return opcode_length + imm.length;
  }

  int DecodeStringNewWtf8Array(unibrow::Utf8Variant variant,
                               uint32_t opcode_length) {
    NON_CONST_ONLY
    Value end = Pop(2, kWasmI32);
    Value start = Pop(1, kWasmI32);
    Value array = PopPackedArray(0, kWasmI8, WasmArrayAccess::kRead);
    bool null_on_invalid = variant == unibrow::Utf8Variant::kUtf8NoTrap;
    Value* result = Push(ValueType::RefMaybeNull(
        HeapType::kString, null_on_invalid ? kNullable : kNonNullable));
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringNewWtf8Array, variant, array,
                                       start, end, result);
    return opcode_length;
  }

  int DecodeStringEncodeWtf8Array(unibrow::Utf8Variant variant,
                                  uint32_t opcode_length) {
    NON_CONST_ONLY
    Value start = Pop(2, kWasmI32);
    Value array = PopPackedArray(1, kWasmI8, WasmArrayAccess::kWrite);
    Value str = Pop(0, kWasmStringRef);
    Value* result = Push(kWasmI32);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(StringEncodeWtf8Array, variant, str,
                                       array, start, result);
    return opcode_length;
  }

  int DecodeStringRefOpcode(WasmOpcode opcode, uint32_t opcode_length) {
    // Fast check for out-of-range opcodes (only allow 0xfbXX).
    // This might help the big switch below.
    if (!VALIDATE((opcode >> 8) == kGCPrefix)) {
      this->DecodeError("invalid stringref opcode: %x", opcode);
      return 0;
    }

    switch (opcode) {
      case kExprStringNewUtf8:
        return DecodeStringNewWtf8(unibrow::Utf8Variant::kUtf8, opcode_length);
      case kExprStringNewUtf8Try:
        return DecodeStringNewWtf8(unibrow::Utf8Variant::kUtf8NoTrap,
                                   opcode_length);
      case kExprStringNewLossyUtf8:
        return DecodeStringNewWtf8(unibrow::Utf8Variant::kLossyUtf8,
                                   opcode_length);
      case kExprStringNewWtf8:
        return DecodeStringNewWtf8(unibrow::Utf8Variant::kWtf8, opcode_length);
      case kExprStringNewWtf16: {
        NON_CONST_ONLY
        MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType addr_type = MemoryIndexType(imm.memory);
        auto [offset, size] = Pop(addr_type, kWasmI32);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringNewWtf16, imm, offset, size,
                                           result);
        return opcode_length + imm.length;
      }
      case kExprStringConst: {
        StringConstImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringConst, imm, result);
        return opcode_length + imm.length;
      }
      case kExprStringMeasureUtf8:
        return DecodeStringMeasureWtf8(unibrow::Utf8Variant::kUtf8,
                                       opcode_length);
      case kExprStringMeasureWtf8:
        return DecodeStringMeasureWtf8(unibrow::Utf8Variant::kWtf8,
                                       opcode_length);
      case kExprStringMeasureWtf16: {
        NON_CONST_ONLY
        Value str = Pop(kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringMeasureWtf16, str, result);
        return opcode_length;
      }
      case kExprStringEncodeUtf8:
        return DecodeStringEncodeWtf8(unibrow::Utf8Variant::kUtf8,
                                      opcode_length);
      case kExprStringEncodeLossyUtf8:
        return DecodeStringEncodeWtf8(unibrow::Utf8Variant::kLossyUtf8,
                                      opcode_length);
      case kExprStringEncodeWtf8:
        return DecodeStringEncodeWtf8(unibrow::Utf8Variant::kWtf8,
                                      opcode_length);
      case kExprStringEncodeWtf16: {
        NON_CONST_ONLY
        MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType addr_type = MemoryIndexType(imm.memory);
        auto [str, addr] = Pop(kWasmStringRef, addr_type);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringEncodeWtf16, imm, str, addr,
                                           result);
        return opcode_length + imm.length;
      }
      case kExprStringConcat: {
        NON_CONST_ONLY
        auto [head, tail] = Pop(kWasmStringRef, kWasmStringRef);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringConcat, head, tail, result);
        return opcode_length;
      }
      case kExprStringEq: {
        NON_CONST_ONLY
        auto [a, b] = Pop(kWasmStringRef, kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringEq, a, b, result);
        return opcode_length;
      }
      case kExprStringIsUSVSequence: {
        NON_CONST_ONLY
        Value str = Pop(kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringIsUSVSequence, str, result);
        return opcode_length;
      }
      case kExprStringAsWtf8: {
        NON_CONST_ONLY
        Value str = Pop(kWasmStringRef);
        Value* result = Push(ValueType::Ref(HeapType::kStringViewWtf8));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringAsWtf8, str, result);
        return opcode_length;
      }
      case kExprStringViewWtf8Advance: {
        NON_CONST_ONLY
        auto [view, pos, bytes] = Pop(kWasmStringViewWtf8, kWasmI32, kWasmI32);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf8Advance, view, pos,
                                           bytes, result);
        return opcode_length;
      }
      case kExprStringViewWtf8EncodeUtf8:
        return DecodeStringViewWtf8Encode(unibrow::Utf8Variant::kUtf8,
                                          opcode_length);
      case kExprStringViewWtf8EncodeLossyUtf8:
        return DecodeStringViewWtf8Encode(unibrow::Utf8Variant::kLossyUtf8,
                                          opcode_length);
      case kExprStringViewWtf8EncodeWtf8:
        return DecodeStringViewWtf8Encode(unibrow::Utf8Variant::kWtf8,
                                          opcode_length);
      case kExprStringViewWtf8Slice: {
        NON_CONST_ONLY
        auto [view, start, end] = Pop(kWasmStringViewWtf8, kWasmI32, kWasmI32);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf8Slice, view, start,
                                           end, result);
        return opcode_length;
      }
      case kExprStringAsWtf16: {
        NON_CONST_ONLY
        Value str = Pop(kWasmStringRef);
        Value* result = Push(ValueType::Ref(HeapType::kStringViewWtf16));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringAsWtf16, str, result);
        return opcode_length;
      }
      case kExprStringViewWtf16Length: {
        NON_CONST_ONLY
        Value view = Pop(kWasmStringViewWtf16);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringMeasureWtf16, view, result);
        return opcode_length;
      }
      case kExprStringViewWtf16GetCodeunit: {
        NON_CONST_ONLY
        auto [view, pos] = Pop(kWasmStringViewWtf16, kWasmI32);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf16GetCodeUnit, view,
                                           pos, result);
        return opcode_length;
      }
      case kExprStringViewWtf16Encode: {
        NON_CONST_ONLY
        MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType addr_type = MemoryIndexType(imm.memory);
        auto [view, addr, pos, codeunits] =
            Pop(kWasmStringViewWtf16, addr_type, kWasmI32, kWasmI32);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf16Encode, imm, view,
                                           addr, pos, codeunits, result);
        return opcode_length + imm.length;
      }
      case kExprStringViewWtf16Slice: {
        NON_CONST_ONLY
        auto [view, start, end] = Pop(kWasmStringViewWtf16, kWasmI32, kWasmI32);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewWtf16Slice, view, start,
                                           end, result);
        return opcode_length;
      }
      case kExprStringAsIter: {
        NON_CONST_ONLY
        Value str = Pop(kWasmStringRef);
        Value* result = Push(ValueType::Ref(HeapType::kStringViewIter));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringAsIter, str, result);
        return opcode_length;
      }
      case kExprStringViewIterNext: {
        NON_CONST_ONLY
        Value view = Pop(kWasmStringViewIter);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewIterNext, view, result);
        return opcode_length;
      }
      case kExprStringViewIterAdvance: {
        NON_CONST_ONLY
        auto [view, codepoints] = Pop(kWasmStringViewIter, kWasmI32);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewIterAdvance, view,
                                           codepoints, result);
        return opcode_length;
      }
      case kExprStringViewIterRewind: {
        NON_CONST_ONLY
        auto [view, codepoints] = Pop(kWasmStringViewIter, kWasmI32);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewIterRewind, view,
                                           codepoints, result);
        return opcode_length;
      }
      case kExprStringViewIterSlice: {
        NON_CONST_ONLY
        auto [view, codepoints] = Pop(kWasmStringViewIter, kWasmI32);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringViewIterSlice, view,
                                           codepoints, result);
        return opcode_length;
      }
      case kExprStringNewUtf8Array:
        return DecodeStringNewWtf8Array(unibrow::Utf8Variant::kUtf8,
                                        opcode_length);
      case kExprStringNewUtf8ArrayTry:
        return DecodeStringNewWtf8Array(unibrow::Utf8Variant::kUtf8NoTrap,
                                        opcode_length);
      case kExprStringNewLossyUtf8Array:
        return DecodeStringNewWtf8Array(unibrow::Utf8Variant::kLossyUtf8,
                                        opcode_length);
      case kExprStringNewWtf8Array:
        return DecodeStringNewWtf8Array(unibrow::Utf8Variant::kWtf8,
                                        opcode_length);
      case kExprStringNewWtf16Array: {
        NON_CONST_ONLY
        Value end = Pop(2, kWasmI32);
        Value start = Pop(1, kWasmI32);
        Value array = PopPackedArray(0, kWasmI16, WasmArrayAccess::kRead);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringNewWtf16Array, array, start,
                                           end, result);
        return opcode_length;
      }
      case kExprStringEncodeUtf8Array:
        return DecodeStringEncodeWtf8Array(unibrow::Utf8Variant::kUtf8,
                                           opcode_length);
      case kExprStringEncodeLossyUtf8Array:
        return DecodeStringEncodeWtf8Array(unibrow::Utf8Variant::kLossyUtf8,
                                           opcode_length);
      case kExprStringEncodeWtf8Array:
        return DecodeStringEncodeWtf8Array(unibrow::Utf8Variant::kWtf8,
                                           opcode_length);
      case kExprStringEncodeWtf16Array: {
        NON_CONST_ONLY
        Value start = Pop(2, kWasmI32);
        Value array = PopPackedArray(1, kWasmI16, WasmArrayAccess::kWrite);
        Value str = Pop(0, kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringEncodeWtf16Array, str, array,
                                           start, result);
        return opcode_length;
      }
      case kExprStringCompare: {
        NON_CONST_ONLY
        auto [lhs, rhs] = Pop(kWasmStringRef, kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringCompare, lhs, rhs, result);
        return opcode_length;
      }
      case kExprStringFromCodePoint: {
        NON_CONST_ONLY
        Value code_point = Pop(kWasmI32);
        Value* result = Push(kWasmRefString);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringFromCodePoint, code_point,
                                           result);
        return opcode_length;
      }
      case kExprStringHash: {
        NON_CONST_ONLY
        Value string = Pop(kWasmStringRef);
        Value* result = Push(kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(StringHash, string, result);
        return opcode_length;
      }
      default:
        this->DecodeError("invalid stringref opcode: %x", opcode);
        return 0;
    }
  }
#undef NON_CONST_ONLY

  uint32_t DecodeAtomicOpcode(WasmOpcode opcode, uint32_t opcode_length) {
    // Fast check for out-of-range opcodes (only allow 0xfeXX).
    if (!VALIDATE((opcode >> 8) == kAtomicPrefix)) {
      this->DecodeError("invalid atomic opcode: 0x%x", opcode);
      return 0;
    }

    MachineType memtype;
    switch (opcode) {
#define CASE_ATOMIC_STORE_OP(Name, Type)          \
  case kExpr##Name: {                             \
    memtype = MachineType::Type();                \
    break; /* to generic mem access code below */ \
  }
      ATOMIC_STORE_OP_LIST(CASE_ATOMIC_STORE_OP)
#undef CASE_ATOMIC_STORE_OP
#define CASE_ATOMIC_OP(Name, Type)                \
  case kExpr##Name: {                             \
    memtype = MachineType::Type();                \
    break; /* to generic mem access code below */ \
  }
      ATOMIC_OP_LIST(CASE_ATOMIC_OP)
#undef CASE_ATOMIC_OP
      case kExprAtomicFence: {
        uint8_t zero = this->template read_u8<ValidationTag>(
            this->pc_ + opcode_length, "zero");
        if (!VALIDATE(zero == 0)) {
          this->DecodeError(this->pc_ + opcode_length,
                            "invalid atomic operand");
          return 0;
        }
        CALL_INTERFACE_IF_OK_AND_REACHABLE(AtomicFence);
        return 1 + opcode_length;
      }
      default:
        // This path is only possible if we are validating.
        V8_ASSUME(ValidationTag::validate);
        this->DecodeError("invalid atomic opcode: 0x%x", opcode);
        return 0;
    }

    const uint32_t element_size_log2 =
        ElementSizeLog2Of(memtype.representation());
    MemoryAccessImmediate imm =
        MakeMemoryAccessImmediate(opcode_length, element_size_log2);
    if (!this->Validate(this->pc_ + opcode_length, imm)) return false;
    if (!VALIDATE(imm.alignment == element_size_log2)) {
      this->DecodeError(this->pc_,
                        "invalid alignment for atomic operation; expected "
                        "alignment is %u, actual alignment is %u",
                        element_size_log2, imm.alignment);
    }

    const FunctionSig* sig =
        WasmOpcodes::SignatureForAtomicOp(opcode, imm.memory->is_memory64);
    V8_ASSUME(sig != nullptr);
    PoppedArgVector args = PopArgs(sig);
    Value* result = sig->return_count() ? Push(sig->GetReturn()) : nullptr;
    if (V8_LIKELY(!CheckStaticallyOutOfBounds(imm.memory, memtype.MemSize(),
                                              imm.offset))) {
      CALL_INTERFACE_IF_OK_AND_REACHABLE(AtomicOp, opcode, args.data(),
                                         sig->parameter_count(), imm, result);
    }

    return opcode_length + imm.length;
  }

  unsigned DecodeNumericOpcode(WasmOpcode opcode, uint32_t opcode_length) {
    // Fast check for out-of-range opcodes (only allow 0xfcXX).
    // This avoids a dynamic check in signature lookup, and might also help the
    // big switch below.
    if (!VALIDATE((opcode >> 8) == kNumericPrefix)) {
      this->DecodeError("invalid numeric opcode: 0x%x", opcode);
      return 0;
    }

    const FunctionSig* sig = WasmOpcodes::Signature(opcode);
    switch (opcode) {
      case kExprI32SConvertSatF32:
      case kExprI32UConvertSatF32:
      case kExprI32SConvertSatF64:
      case kExprI32UConvertSatF64:
      case kExprI64SConvertSatF32:
      case kExprI64UConvertSatF32:
      case kExprI64SConvertSatF64:
      case kExprI64UConvertSatF64: {
        BuildSimpleOperator(opcode, sig);
        return opcode_length;
      }
      case kExprMemoryInit: {
        MemoryInitImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType mem_type = MemoryIndexType(imm.memory.memory);
        auto [dst, offset, size] = Pop(mem_type, kWasmI32, kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(MemoryInit, imm, dst, offset, size);
        return opcode_length + imm.length;
      }
      case kExprDataDrop: {
        IndexImmediate imm(this, this->pc_ + opcode_length,
                           "data segment index", validate);
        if (!this->ValidateDataSegment(this->pc_ + opcode_length, imm)) {
          return 0;
        }
        CALL_INTERFACE_IF_OK_AND_REACHABLE(DataDrop, imm);
        return opcode_length + imm.length;
      }
      case kExprMemoryCopy: {
        MemoryCopyImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType dst_type = MemoryIndexType(imm.memory_dst.memory);
        ValueType src_type = MemoryIndexType(imm.memory_src.memory);
        // size_type = min(dst_type, src_type), where kI32 < kI64.
        ValueType size_type = dst_type == kWasmI32 ? kWasmI32 : src_type;

        auto [dst, src, size] = Pop(dst_type, src_type, size_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(MemoryCopy, imm, dst, src, size);
        return opcode_length + imm.length;
      }
      case kExprMemoryFill: {
        MemoryIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType mem_type = MemoryIndexType(imm.memory);
        auto [dst, value, size] = Pop(mem_type, kWasmI32, mem_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(MemoryFill, imm, dst, value, size);
        return opcode_length + imm.length;
      }
      case kExprTableInit: {
        TableInitImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType table_index_type = TableIndexType(imm.table.table);
        auto [dst, src, size] = Pop(table_index_type, kWasmI32, kWasmI32);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(TableInit, imm, dst, src, size);
        return opcode_length + imm.length;
      }
      case kExprElemDrop: {
        IndexImmediate imm(this, this->pc_ + opcode_length,
                           "element segment index", validate);
        if (!this->ValidateElementSegment(this->pc_ + opcode_length, imm)) {
          return 0;
        }
        CALL_INTERFACE_IF_OK_AND_REACHABLE(ElemDrop, imm);
        return opcode_length + imm.length;
      }
      case kExprTableCopy: {
        TableCopyImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType dst_type = TableIndexType(imm.table_dst.table);
        ValueType src_type = TableIndexType(imm.table_src.table);
        // size_type = min(dst_type, src_type), where kI32 < kI64.
        ValueType size_type = dst_type == kWasmI32 ? kWasmI32 : src_type;

        auto [dst, src, size] = Pop(dst_type, src_type, size_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(TableCopy, imm, dst, src, size);
        return opcode_length + imm.length;
      }
      case kExprTableGrow: {
        TableIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType table_index_type = TableIndexType(imm.table);
        auto [value, delta] = Pop(imm.table->type, table_index_type);
        Value* result = Push(table_index_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(TableGrow, imm, value, delta,
                                           result);
        return opcode_length + imm.length;
      }
      case kExprTableSize: {
        TableIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        Value* result = Push(TableIndexType(imm.table));
        CALL_INTERFACE_IF_OK_AND_REACHABLE(TableSize, imm, result);
        return opcode_length + imm.length;
      }
      case kExprTableFill: {
        TableIndexImmediate imm(this, this->pc_ + opcode_length, validate);
        if (!this->Validate(this->pc_ + opcode_length, imm)) return 0;
        ValueType table_index_type = TableIndexType(imm.table);
        auto [start, value, count] =
            Pop(table_index_type, imm.table->type, table_index_type);
        CALL_INTERFACE_IF_OK_AND_REACHABLE(TableFill, imm, start, value, count);
        return opcode_length + imm.length;
      }
      case kExprF32LoadMemF16: {
        if (!v8_flags.experimental_wasm_fp16) {
          this->DecodeError(
              "invalid numeric opcode: 0x%x, "
              "enable with --experimental-wasm-fp16",
              opcode);
          return 0;
        }
        return DecodeLoadMem(LoadType::kF32LoadF16, 2);
      }
      case kExprF32StoreMemF16: {
        if (!v8_flags.experimental_wasm_fp16) {
          this->DecodeError(
              "invalid numeric opcode: 0x%x, "
              "enable with --experimental-wasm-fp16",
              opcode);
          return 0;
        }
        return DecodeStoreMem(StoreType::kF32StoreF16, 2);
      }
      default:
        this->DecodeError("invalid numeric opcode: 0x%x", opcode);
        return 0;
    }
  }

  V8_INLINE Value CreateValue(ValueType type) { return Value{this->pc_, type}; }

  V8_INLINE Value* Push(Value value) {
    DCHECK_IMPLIES(this->ok(), value.type != kWasmVoid);
    if (!VALIDATE(!this->is_shared_ || IsShared(value.type, this->module_))) {
      this->DecodeError(value.pc(), "%s does not have a shared type",
                        SafeOpcodeNameAt(value.pc()));
      return nullptr;
    }
    // {stack_.EnsureMoreCapacity} should have been called before, either in the
    // central decoding loop, or individually if more than one element is
    // pushed.
    stack_.push(value);
    return &stack_.back();
  }

  V8_INLINE Value* Push(ValueType type) { return Push(CreateValue(type)); }

  void PushMergeValues(Control* c, Merge<Value>* merge) {
    if constexpr (decoding_mode == kConstantExpression) return;
    DCHECK_EQ(c, &control_.back());
    DCHECK(merge == &c->start_merge || merge == &c->end_merge);
    stack_.shrink_to(c->stack_depth);
    if (merge->arity == 1) {
      // {stack_.EnsureMoreCapacity} should have been called before in the
      // central decoding loop.
      Push(merge->vals.first);
    } else {
      stack_.EnsureMoreCapacity(merge->arity, this->zone_);
      for (uint32_t i = 0; i < merge->arity; i++) {
        Push(merge->vals.array[i]);
      }
    }
    DCHECK_EQ(c->stack_depth + merge->arity, stack_.size());
  }

  V8_INLINE void PushReturns(ReturnVector values) {
    stack_.EnsureMoreCapacity(static_cast<int>(values.size()), this->zone_);
    for (Value& value : values) Push(value);
  }

  Value* PushReturns(const FunctionSig* sig) {
    size_t return_count = sig->return_count();
    stack_.EnsureMoreCapacity(static_cast<int>(return_count), this->zone_);
    for (size_t i = 0; i < return_count; ++i) {
      Push(sig->GetReturn(i));
    }
    return stack_.end() - return_count;
  }

  // We do not inline these functions because doing so causes a large binary
  // size increase. Not inlining them should not create a performance
  // degradation, because their invocations are guarded by V8_LIKELY.
  V8_NOINLINE V8_PRESERVE_MOST void PopTypeError(int index, Value val,
                                                 const char* expected) {
    this->DecodeError(val.pc(), "%s[%d] expected %s, found %s of type %s",
                      SafeOpcodeNameAt(this->pc_), index, expected,
                      SafeOpcodeNameAt(val.pc()), val.type.name().c_str());
  }

  V8_NOINLINE V8_PRESERVE_MOST void PopTypeError(int index, Value val,
                                                 std::string expected) {
    PopTypeError(index, val, expected.c_str());
  }

  V8_NOINLINE V8_PRESERVE_MOST void PopTypeError(int index, Value val,
                                                 ValueType expected) {
    PopTypeError(index, val, ("type " + expected.name()).c_str());
  }

  V8_NOINLINE V8_PRESERVE_MOST void NotEnoughArgumentsError(int needed,
                                                            int actual) {
    DCHECK_LT(0, needed);
    DCHECK_LE(0, actual);
    DCHECK_LT(actual, needed);
    this->DecodeError(
        "not enough arguments on the stack for %s (need %d, got %d)",
        SafeOpcodeNameAt(this->pc_), needed, actual);
  }

  V8_INLINE Value Pop(int index, ValueType expected) {
    Value value = Pop();
    ValidateStackValue(index, value, expected);
    return value;
  }

  V8_INLINE void ValidateStackValue(int index, Value value,
                                    ValueType expected) {
    if (!VALIDATE(IsSubtypeOf(value.type, expected, this->module_) ||
                  value.type == kWasmBottom || expected == kWasmBottom)) {
      PopTypeError(index, value, expected);
    }
  }

  V8_INLINE Value Pop() {
    DCHECK(!control_.empty());
    uint32_t limit = control_.back().stack_depth;
    if (V8_UNLIKELY(stack_size() <= limit)) {
      // Popping past the current control start in reachable code.
      if (!VALIDATE(control_.back().unreachable())) {
        NotEnoughArgumentsError(1, 0);
      }
      return UnreachableValue(this->pc_);
    }
    Value top_of_stack = stack_.back();
    stack_.pop();
    return top_of_stack;
  }

  V8_INLINE Value Peek(int depth, int index, ValueType expected) {
    Value value = Peek(depth);
    ValidateStackValue(index, value, expected);
    return value;
  }

  V8_INLINE Value Peek(int depth = 0) {
    DCHECK(!control_.empty());
    uint32_t limit = control_.back().stack_depth;
    if (V8_UNLIKELY(stack_.size() <= limit + depth)) {
      // Peeking past the current control start in reachable code.
      if (!VALIDATE(decoding_mode == kFunctionBody &&
                    control_.back().unreachable())) {
        NotEnoughArgumentsError(depth + 1, stack_.size() - limit);
      }
      return UnreachableValue(this->pc_);
    }
    DCHECK_LT(depth, stack_.size());
    return *(stack_.end() - depth - 1);
  }

  V8_INLINE Value Peek(ValueType expected) { return Peek(0, 0, expected); }

  // Pop multiple values at once; faster than multiple individual {Pop}s.
  // Returns an array of the popped values if there are multiple, or the popped
  // value itself if a single type is passed.
  template <typename... ValueTypes,
            typename = std::enable_if_t<
                // Pop is only allowed to be called with ValueType parameters.
                std::conjunction_v<std::is_same<ValueType, ValueTypes>...>>>
  V8_INLINE std::conditional_t<sizeof...(ValueTypes) == 1, Value,
                               std::array<Value, sizeof...(ValueTypes)>>
  Pop(ValueTypes... expected_types) {
    constexpr int kCount = sizeof...(ValueTypes);
    EnsureStackArguments(kCount);
    DCHECK_LE(control_.back().stack_depth, stack_size());
    DCHECK_GE(stack_size() - control_.back().stack_depth, kCount);
    // Note: Popping from the {FastZoneVector} does not invalidate the old (now
    // out-of-range) elements.
    stack_.pop(kCount);
    auto ValidateAndGetNextArg = [this, i = 0](ValueType type) mutable {
      ValidateStackValue(i, stack_.end()[i], type);
      return stack_.end()[i++];
    };
    return {ValidateAndGetNextArg(expected_types)...};
  }

  Value PopPackedArray(uint32_t operand_index, ValueType expected_element_type,
                       WasmArrayAccess access) {
    Value array = Pop();
    if (array.type.is_bottom()) {
      // We are in a polymorphic stack. Leave the stack as it is.
      DCHECK(!current_code_reachable_and_ok_);
      return array;
    }
    // Inputs of type "none" are okay due to implicit upcasting. The stringref
    // spec doesn't say this explicitly yet, but it's consistent with the rest
    // of Wasm. (Of course such inputs will trap at runtime.) See:
    // https://github.com/WebAssembly/stringref/issues/66
    if (array.type.is_reference_to(HeapType::kNone)) return array;
    if (VALIDATE(array.type.is_object_reference() && array.type.has_index())) {
      uint32_t ref_index = array.type.ref_index();
      if (VALIDATE(this->module_->has_array(ref_index))) {
        const ArrayType* array_type = this->module_->array_type(ref_index);
        if (VALIDATE(array_type->element_type() == expected_element_type &&
                     (access == WasmArrayAccess::kRead ||
                      array_type->mutability()))) {
          return array;
        }
      }
    }
    PopTypeError(operand_index, array,
                 (std::string("array of ") +
                  (access == WasmArrayAccess::kWrite ? "mutable " : "") +
                  expected_element_type.name())
                     .c_str());
    return array;
  }

  // Drop the top {count} stack elements, or all of them if less than {count}
  // are present.
  V8_INLINE void Drop(int count = 1) {
    DCHECK(!control_.empty());
    uint32_t limit = control_.back().stack_depth;
    if (V8_UNLIKELY(stack_.size() < limit + count)) {
      // Pop what we can.
      count = std::min(count, static_cast<int>(stack_.size() - limit));
    }
    stack_.pop(count);
  }
  // Drop the top stack element if present. Takes a Value input for more
  // descriptive call sites.
  V8_INLINE void Drop(const Value& /* unused */) { Drop(1); }

  enum StackElementsCountMode : bool {
    kNonStrictCounting = false,
    kStrictCounting = true
  };

  enum MergeType {
    kBranchMerge,
    kReturnMerge,
    kFallthroughMerge,
    kInitExprMerge
  };

  enum class PushBranchValues : bool {
    kNo = false,
    kYes = true,
  };
  enum class RewriteStackTypes : bool {
    kNo = false,
    kYes = true,
  };

  // - If the current code is reachable, check if the current stack values are
  //   compatible with {merge} based on their number and types. If
  //   {strict_count}, check that #(stack elements) == {merge->arity}, otherwise
  //   #(stack elements) >= {merge->arity}.
  // - If the current code is unreachable, check if any values that may exist on
  //   top of the stack are compatible with {merge}. If {push_branch_values},
  //   push back to the stack values based on the type of {merge} (this is
  //   needed for conditional branches due to their typing rules, and
  //   fallthroughs so that the outer control finds the expected values on the
  //   stack). TODO(manoskouk): We expect the unreachable-code behavior to
  //   change, either due to relaxation of dead code verification, or the
  //   introduction of subtyping.
  template <StackElementsCountMode strict_count,
            PushBranchValues push_branch_values, MergeType merge_type,
            RewriteStackTypes rewrite_types>
  V8_INLINE bool TypeCheckStackAgainstMerge(Merge<Value>* merge) {
    uint32_t arity = merge->arity;
    uint32_t actual = stack_.size() - control_.back().stack_depth;
    // Handle trivial cases first. Arity 0 is the most common case.
    if (arity == 0 && (!strict_count || actual == 0)) return true;
    // Arity 1 is still common enough that we handle it separately (only doing
    // the most basic subtype check).
    if (arity == 1 && (strict_count ? actual == arity : actual >= arity)) {
      if (stack_.back().type == merge->vals.first.type) return true;
    }
    return TypeCheckStackAgainstMerge_Slow<strict_count, push_branch_values,
                                           merge_type, rewrite_types>(merge);
  }

  // Slow path for {TypeCheckStackAgainstMerge}.
  template <StackElementsCountMode strict_count,
            PushBranchValues push_branch_values, MergeType merge_type,
            RewriteStackTypes rewrite_types>
  V8_PRESERVE_MOST V8_NOINLINE bool TypeCheckStackAgainstMerge_Slow(
      Merge<Value>* merge) {
    constexpr const char* merge_description =
        merge_type == kBranchMerge     ? "branch"
        : merge_type == kReturnMerge   ? "return"
        : merge_type == kInitExprMerge ? "constant expression"
                                       : "fallthru";
    uint32_t arity = merge->arity;
    uint32_t actual = stack_.size() - control_.back().stack_depth;
    // Here we have to check for !unreachable(), because we need to typecheck as
    // if the current code is reachable even if it is spec-only reachable.
    if (V8_LIKELY(decoding_mode == kConstantExpression ||
                  !control_.back().unreachable())) {
      if (V8_UNLIKELY(strict_count ? actual != arity : actual < arity)) {
        this->DecodeError("expected %u elements on the stack for %s, found %u",
                          arity, merge_description, actual);
        return false;
      }
      // Typecheck the topmost {merge->arity} values on the stack.
      Value* stack_values = stack_.end() - arity;
      for (uint32_t i = 0; i < arity; ++i) {
        Value& val = stack_values[i];
        Value& old = (*merge)[i];
        if (!IsSubtypeOf(val.type, old.type, this->module_)) {
          this->DecodeError("type error in %s[%u] (expected %s, got %s)",
                            merge_description, i, old.type.name().c_str(),
                            val.type.name().c_str());
          return false;
        }
        if constexpr (static_cast<bool>(rewrite_types)) {
          // Upcast type on the stack to the target type of the label.
          val.type = old.type;
        }
      }
      return true;
    }
    // Unreachable code validation starts here.
    if (V8_UNLIKELY(strict_count && actual > arity)) {
      this->DecodeError("expected %u elements on the stack for %s, found %u",
                        arity, merge_description, actual);
      return false;
    }
    // TODO(manoskouk): Use similar code as above if we keep unreachable checks.
    for (int i = arity - 1, depth = 0; i >= 0; --i, ++depth) {
      Peek(depth, i, (*merge)[i].type);
    }
    if constexpr (static_cast<bool>(push_branch_values)) {
      uint32_t inserted_value_count =
          static_cast<uint32_t>(EnsureStackArguments(arity));
      if (inserted_value_count > 0) {
        // stack_.EnsureMoreCapacity() may have inserted unreachable values into
        // the bottom of the stack. If so, mark them with the correct type. If
        // drop values were also inserted, disregard them, as they will be
        // dropped anyway.
        Value* stack_base = stack_value(arity);
        for (uint32_t i = 0; i < std::min(arity, inserted_value_count); i++) {
          if (stack_base[i].type == kWasmBottom) {
            stack_base[i].type = (*merge)[i].type;
          }
        }
      }
    }
    return VALIDATE(this->ok());
  }

  template <StackElementsCountMode strict_count, MergeType merge_type>
  bool DoReturn() {
    if (!VALIDATE(
            (TypeCheckStackAgainstMerge<strict_count, PushBranchValues::kNo,
                                        merge_type, RewriteStackTypes::kNo>(
                &control_.front().end_merge)))) {
      return false;
    }
    DCHECK_IMPLIES(current_code_reachable_and_ok_,
                   stack_.size() >= this->sig_->return_count());
    CALL_INTERFACE_IF_OK_AND_REACHABLE(DoReturn, 0);
    EndControl();
    return true;
  }

  int startrel(const uint8_t* ptr) {
    return static_cast<int>(ptr - this->start_);
  }

  void FallThrough() {
    Control* c = &control_.back();
    DCHECK_NE(c->kind, kControlLoop);
    if (!VALIDATE(TypeCheckFallThru())) return;
    CALL_INTERFACE_IF_OK_AND_REACHABLE(FallThruTo, c);
    if (c->reachable()) c->end_merge.reached = true;
  }

  bool TypeCheckOneArmedIf(Control* c) {
    DCHECK(c->is_onearmed_if());
    if (c->end_merge.arity != c->start_merge.arity) {
      this->DecodeError(c->pc(),
                        "start-arity and end-arity of one-armed if must match");
      return false;
    }
    for (uint32_t i = 0; i < c->start_merge.arity; ++i) {
      Value& start = c->start_merge[i];
      Value& end = c->end_merge[i];
      if (!IsSubtypeOf(start.type, end.type, this->module_)) {
        this->DecodeError("type error in merge[%u] (expected %s, got %s)", i,
                          end.type.name().c_str(), start.type.name().c_str());
        return false;
      }
    }
    return true;
  }

  bool TypeCheckFallThru() {
    return TypeCheckStackAgainstMerge<kStrictCounting, PushBranchValues::kYes,
                                      kFallthroughMerge,
                                      RewriteStackTypes::kNo>(
        &control_.back().end_merge);
  }

  // If the current code is reachable, check if the current stack values are
  // compatible with a jump to {c}, based on their number and types.
  // Otherwise, we have a polymorphic stack: check if any values that may exist
  // on top of the stack are compatible with {c}. If {push_branch_values},
  // push back to the stack values based on the type of {c} (this is needed for
  // conditional branches due to their typing rules, and fallthroughs so that
  // the outer control finds enough values on the stack).
  template <PushBranchValues push_branch_values,
            RewriteStackTypes rewrite_types>
  bool TypeCheckBranch(Control* c) {
    return TypeCheckStackAgainstMerge<kNonStrictCounting, push_branch_values,
                                      kBranchMerge, rewrite_types>(
        c->br_merge());
  }

  void onFirstError() override {
    this->end_ = this->pc_;  // Terminate decoding loop.
    this->current_code_reachable_and_ok_ = false;
    TRACE(" !%s\n", this->error_.message().c_str());
    // Cannot use CALL_INTERFACE_* macros because we emitted an error.
    interface().OnFirstError(this);
  }

  // There are currently no simple prototype operators.
  int BuildSimplePrototypeOperator(WasmOpcode opcode) {
    const FunctionSig* sig = WasmOpcodes::Signature(opcode);
    return BuildSimpleOperator(opcode, sig);
  }

  int BuildSimpleOperator(WasmOpcode opcode, const FunctionSig* sig) {
    DCHECK_GE(1, sig->return_count());
    if (sig->parameter_count() == 1) {
      // All current simple unary operators have exactly 1 return value.
      DCHECK_EQ(1, sig->return_count());
      return BuildSimpleOperator(opcode, sig->GetReturn(0), sig->GetParam(0));
    } else {
      DCHECK_EQ(2, sig->parameter_count());
      ValueType ret = sig->return_count() == 0 ? kWasmVoid : sig->GetReturn(0);
      return BuildSimpleOperator(opcode, ret, sig->GetParam(0),
                                 sig->GetParam(1));
    }
  }

  int BuildSimpleOperator(WasmOpcode opcode, ValueType return_type,
                          ValueType arg_type) {
    DCHECK_NE(kWasmVoid, return_type);
    Value val = Pop(arg_type);
    Value* ret = Push(return_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(UnOp, opcode, val, ret);
    return 1;
  }

  int BuildSimpleOperator(WasmOpcode opcode, ValueType return_type,
                          ValueType lhs_type, ValueType rhs_type) {
    auto [lval, rval] = Pop(lhs_type, rhs_type);
    Value* ret = return_type == kWasmVoid ? nullptr : Push(return_type);
    CALL_INTERFACE_IF_OK_AND_REACHABLE(BinOp, opcode, lval, rval, ret);
    return 1;
  }

#define DEFINE_SIMPLE_SIG_OPERATOR(sig, ...)         \
  int BuildSimpleOperator_##sig(WasmOpcode opcode) { \
    return BuildSimpleOperator(opcode, __VA_ARGS__); \
  }
  FOREACH_SIGNATURE(DEFINE_SIMPLE_SIG_OPERATOR)
#undef DEFINE_SIMPLE_SIG_OPERATOR

  static constexpr ValidationTag validate = {};
};

class EmptyInterface {
 public:
  using ValidationTag = Decoder::FullValidationTag;
  static constexpr DecodingMode decoding_mode = kFunctionBody;
  static constexpr bool kUsesPoppedArgs = false;
  using Value = ValueBase<ValidationTag>;
  using Control = ControlBase<Value, ValidationTag>;
  using FullDecoder = WasmFullDecoder<ValidationTag, EmptyInterface>;

#define DEFINE_EMPTY_CALLBACK(name, ...) \
  void name(FullDecoder* decoder, ##__VA_ARGS__) {}
  INTERFACE_FUNCTIONS(DEFINE_EMPTY_CALLBACK)
#undef DEFINE_EMPTY_CALLBACK
};

#undef CALL_INTERFACE_IF_OK_AND_REACHABLE
#undef CALL_INTERFACE_IF_OK_AND_PARENT_REACHABLE
#undef TRACE
#undef TRACE_INST_FORMAT
#undef VALIDATE
#undef CHECK_PROTOTYPE_OPCODE

}  // namespace v8::internal::wasm

#endif  // V8_WASM_FUNCTION_BODY_DECODER_IMPL_H_

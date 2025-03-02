// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_BUILTIN_LIST_H_
#define V8_WASM_WASM_BUILTIN_LIST_H_

#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

namespace v8::internal::wasm {
// Convenience macro listing all builtins called directly from wasm via the far
// jump table. Note that the first few elements of the list coincide with
// {compiler::TrapId}, order matters.
#define WASM_BUILTINS_WITH_JUMP_TABLE_SLOT(V, VTRAP) /*                     */ \
  FOREACH_WASM_TRAPREASON(VTRAP)                                               \
  V(WasmCompileLazy)                                                           \
  V(WasmTriggerTierUp)                                                         \
  V(WasmLiftoffFrameSetup)                                                     \
  V(WasmDebugBreak)                                                            \
  V(WasmInt32ToHeapNumber)                                                     \
  V(WasmFloat64ToString)                                                       \
  V(WasmStringToDouble)                                                        \
  V(WasmIntToString)                                                           \
  V(WasmTaggedNonSmiToInt32)                                                   \
  V(WasmFloat32ToNumber)                                                       \
  V(WasmFloat64ToNumber)                                                       \
  V(WasmTaggedToFloat64)                                                       \
  V(WasmAllocateJSArray)                                                       \
  V(WasmI32AtomicWait)                                                         \
  V(WasmI64AtomicWait)                                                         \
  V(WasmGetOwnProperty)                                                        \
  V(WasmRefFunc)                                                               \
  V(WasmInternalFunctionCreateExternal)                                        \
  V(WasmMemoryGrow)                                                            \
  V(WasmTableInit)                                                             \
  V(WasmTableCopy)                                                             \
  V(WasmTableFill)                                                             \
  V(WasmTableGrow)                                                             \
  V(WasmTableGet)                                                              \
  V(WasmTableSet)                                                              \
  V(WasmTableGetFuncRef)                                                       \
  V(WasmTableSetFuncRef)                                                       \
  V(WasmFunctionTableGet)                                                      \
  V(WasmStackGuard)                                                            \
  V(WasmGrowableStackGuard)                                                    \
  V(WasmStackOverflow)                                                         \
  V(WasmAllocateFixedArray)                                                    \
  V(WasmThrow)                                                                 \
  V(WasmRethrow)                                                               \
  V(WasmThrowRef)                                                              \
  V(WasmRethrowExplicitContext)                                                \
  V(WasmHandleStackOverflow)                                                   \
  V(WasmTraceEnter)                                                            \
  V(WasmTraceExit)                                                             \
  V(WasmTraceMemory)                                                           \
  V(BigIntToI32Pair)                                                           \
  V(BigIntToI64)                                                               \
  V(CallRefIC)                                                                 \
  V(CallIndirectIC)                                                            \
  V(DoubleToI)                                                                 \
  V(I32PairToBigInt)                                                           \
  V(I64ToBigInt)                                                               \
  V(RecordWriteSaveFP)                                                         \
  V(RecordWriteIgnoreFP)                                                       \
  V(ThrowDataViewTypeError)                                                    \
  V(ThrowDataViewDetachedError)                                                \
  V(ThrowDataViewOutOfBounds)                                                  \
  V(ThrowIndexOfCalledOnNull)                                                  \
  V(ThrowToLowerCaseCalledOnNull)                                              \
  IF_INTL(V, StringToLowerCaseIntl)                                            \
  IF_TSAN(V, TSANRelaxedStore8IgnoreFP)                                        \
  IF_TSAN(V, TSANRelaxedStore8SaveFP)                                          \
  IF_TSAN(V, TSANRelaxedStore16IgnoreFP)                                       \
  IF_TSAN(V, TSANRelaxedStore16SaveFP)                                         \
  IF_TSAN(V, TSANRelaxedStore32IgnoreFP)                                       \
  IF_TSAN(V, TSANRelaxedStore32SaveFP)                                         \
  IF_TSAN(V, TSANRelaxedStore64IgnoreFP)                                       \
  IF_TSAN(V, TSANRelaxedStore64SaveFP)                                         \
  IF_TSAN(V, TSANSeqCstStore8IgnoreFP)                                         \
  IF_TSAN(V, TSANSeqCstStore8SaveFP)                                           \
  IF_TSAN(V, TSANSeqCstStore16IgnoreFP)                                        \
  IF_TSAN(V, TSANSeqCstStore16SaveFP)                                          \
  IF_TSAN(V, TSANSeqCstStore32IgnoreFP)                                        \
  IF_TSAN(V, TSANSeqCstStore32SaveFP)                                          \
  IF_TSAN(V, TSANSeqCstStore64IgnoreFP)                                        \
  IF_TSAN(V, TSANSeqCstStore64SaveFP)                                          \
  IF_TSAN(V, TSANRelaxedLoad32IgnoreFP)                                        \
  IF_TSAN(V, TSANRelaxedLoad32SaveFP)                                          \
  IF_TSAN(V, TSANRelaxedLoad64IgnoreFP)                                        \
  IF_TSAN(V, TSANRelaxedLoad64SaveFP)                                          \
  V(WasmAllocateArray_Uninitialized)                                           \
  V(WasmArrayCopy)                                                             \
  V(WasmArrayNewSegment)                                                       \
  V(WasmArrayInitSegment)                                                      \
  V(WasmAllocateStructWithRtt)                                                 \
  V(WasmOnStackReplace)                                                        \
  V(WasmReject)                                                                \
  V(WasmStringNewWtf8)                                                         \
  V(WasmStringNewWtf16)                                                        \
  V(WasmStringConst)                                                           \
  V(WasmStringMeasureUtf8)                                                     \
  V(WasmStringMeasureWtf8)                                                     \
  V(WasmStringEncodeWtf8)                                                      \
  V(WasmStringEncodeWtf16)                                                     \
  V(WasmStringConcat)                                                          \
  V(WasmStringEqual)                                                           \
  V(WasmStringIsUSVSequence)                                                   \
  V(WasmStringAsWtf16)                                                         \
  V(WasmStringViewWtf16GetCodeUnit)                                            \
  V(WasmStringCodePointAt)                                                     \
  V(WasmStringViewWtf16Encode)                                                 \
  V(WasmStringViewWtf16Slice)                                                  \
  V(WasmStringNewWtf8Array)                                                    \
  V(WasmStringNewWtf16Array)                                                   \
  V(WasmStringEncodeWtf8Array)                                                 \
  V(WasmStringToUtf8Array)                                                     \
  V(WasmStringEncodeWtf16Array)                                                \
  V(WasmStringAsWtf8)                                                          \
  V(WasmStringViewWtf8Advance)                                                 \
  V(WasmStringViewWtf8Encode)                                                  \
  V(WasmStringViewWtf8Slice)                                                   \
  V(WasmStringAsIter)                                                          \
  V(WasmStringViewIterNext)                                                    \
  V(WasmStringViewIterAdvance)                                                 \
  V(WasmStringViewIterRewind)                                                  \
  V(WasmStringViewIterSlice)                                                   \
  V(StringCompare)                                                             \
  V(StringIndexOf)                                                             \
  V(WasmStringFromCodePoint)                                                   \
  V(WasmStringHash)                                                            \
  V(WasmAnyConvertExtern)                                                      \
  V(WasmStringFromDataSegment)                                                 \
  V(StringAdd_CheckNone)                                                       \
  V(DebugPrintFloat64)                                                         \
  V(DebugPrintWordPtr)                                                         \
  V(WasmFastApiCallTypeCheckAndUpdateIC)                                       \
  V(DeoptimizationEntry_Eager)                                                 \
  V(WasmLiftoffDeoptFinish)                                                    \
  V(WasmPropagateException)                                                    \
  IF_SHADOW_STACK(V, AdaptShadowStackForDeopt)

// Other wasm builtins that are not called via the far jump table, but need the
// {is_wasm} assembler option for proper stack-switching support.
#define WASM_BUILTINS_WITHOUT_JUMP_TABLE_SLOT(V) \
  V(IterableToFixedArrayForWasm)                 \
  V(WasmAllocateInYoungGeneration)               \
  V(WasmAllocateInOldGeneration)                 \
  V(WasmAllocateZeroedFixedArray)                \
  V(WasmSuspend)                                 \
  V(WasmToJsWrapperInvalidSig)                   \
  V(WasmTrap)                                    \
  V(WasmTrapHandlerThrowTrap)

#define WASM_BUILTIN_LIST(V, VTRAP)            \
  WASM_BUILTINS_WITH_JUMP_TABLE_SLOT(V, VTRAP) \
  WASM_BUILTINS_WITHOUT_JUMP_TABLE_SLOT(V)

namespace detail {
constexpr std::array<uint8_t, static_cast<int>(Builtin::kFirstBytecodeHandler)>
InitBuiltinToFarJumpTableIndex() {
  std::array<uint8_t, static_cast<int>(Builtin::kFirstBytecodeHandler)>
      result{};
  uint8_t next_index = 0;
#define DEF_INIT_LOOKUP(NAME) \
  result[static_cast<int>(Builtin::k##NAME)] = next_index++;
#define DEF_INIT_LOOKUP_TRAP(NAME) DEF_INIT_LOOKUP(ThrowWasm##NAME)
  WASM_BUILTINS_WITH_JUMP_TABLE_SLOT(DEF_INIT_LOOKUP, DEF_INIT_LOOKUP_TRAP)
#undef DEF_INIT_LOOKUP_TRAP
#undef DEF_INIT_LOOKUP
  return result;
}
}  // namespace detail
class BuiltinLookup {
 public:
  static constexpr int JumptableIndexForBuiltin(Builtin builtin) {
    int result = kBuiltinToFarJumpTableIndex[static_cast<int>(builtin)];
    DCHECK_EQ(builtin, kFarJumpTableIndexToBuiltin[result]);
    return result;
  }

  static constexpr Builtin BuiltinForJumptableIndex(int index) {
    Builtin result = kFarJumpTableIndexToBuiltin[index];
    DCHECK_EQ(index, kBuiltinToFarJumpTableIndex[static_cast<int>(result)]);
    return result;
  }

  static constexpr int BuiltinCount() { return kBuiltinCount; }

  static bool IsWasmBuiltinId(Builtin id) {
    switch (id) {
#define BUILTIN_ID(Name) \
  case Builtin::k##Name: \
    return true;
#define BUILTIN_ID_TRAP(Name)     \
  case Builtin::kThrowWasm##Name: \
    return true;
      WASM_BUILTIN_LIST(BUILTIN_ID, BUILTIN_ID_TRAP)
      default:
        return false;
    }
  }

 private:
#define BUILTIN_COUNTER(NAME) +1
  static constexpr int kBuiltinCount =
      0 WASM_BUILTINS_WITH_JUMP_TABLE_SLOT(BUILTIN_COUNTER, BUILTIN_COUNTER);
#undef BUILTIN_COUNTER

  static constexpr auto kFarJumpTableIndexToBuiltin =
      base::make_array<static_cast<int>(kBuiltinCount)>([](size_t index) {
        size_t next_index = 0;
#define DEF_INIT_LOOKUP(NAME) \
  if (index == next_index) {  \
    return Builtin::k##NAME;  \
  }                           \
  ++next_index;
#define DEF_INIT_LOOKUP_TRAP(NAME) DEF_INIT_LOOKUP(ThrowWasm##NAME)
        WASM_BUILTINS_WITH_JUMP_TABLE_SLOT(DEF_INIT_LOOKUP,
                                           DEF_INIT_LOOKUP_TRAP)
#undef DEF_INIT_LOOKUP_TRAP
#undef DEF_INIT_LOOKUP
        return Builtin::kNoBuiltinId;
      });

  static constexpr auto kBuiltinToFarJumpTableIndex =
      detail::InitBuiltinToFarJumpTableIndex();
};

}  // namespace v8::internal::wasm

#undef WASM_BUILTIN_LIST
#undef WASM_BUILTINS_WITHOUT_JUMP_TABLE_SLOT
#undef WASM_BUILTINS_WITH_JUMP_TABLE_SLOT

#endif  // V8_WASM_WASM_BUILTIN_LIST_H_

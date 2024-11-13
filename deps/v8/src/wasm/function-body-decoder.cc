// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/function-body-decoder.h"

#include "src/utils/ostreams.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

template <typename ValidationTag>
bool DecodeLocalDecls(WasmEnabledFeatures enabled, BodyLocalDecls* decls,
                      const WasmModule* module, bool is_shared,
                      const uint8_t* start, const uint8_t* end, Zone* zone) {
  if constexpr (ValidationTag::validate) DCHECK_NOT_NULL(module);
  WasmDetectedFeatures unused_detected_features;
  constexpr FixedSizeSignature<ValueType, 0, 0> kNoSig;
  WasmDecoder<ValidationTag> decoder(zone, module, enabled,
                                     &unused_detected_features, &kNoSig,
                                     is_shared, start, end);
  decls->encoded_size = decoder.DecodeLocals(decoder.pc());
  if (ValidationTag::validate && decoder.failed()) {
    DCHECK_EQ(0, decls->encoded_size);
    return false;
  }
  DCHECK(decoder.ok());
  // Copy the decoded locals types into {decls->local_types}.
  DCHECK_NULL(decls->local_types);
  decls->num_locals = decoder.num_locals_;
  decls->local_types = decoder.local_types_;
  return true;
}

void DecodeLocalDecls(WasmEnabledFeatures enabled, BodyLocalDecls* decls,
                      const uint8_t* start, const uint8_t* end, Zone* zone) {
  constexpr WasmModule* kNoModule = nullptr;
  DecodeLocalDecls<Decoder::NoValidationTag>(enabled, decls, kNoModule, false,
                                             start, end, zone);
}

bool ValidateAndDecodeLocalDeclsForTesting(WasmEnabledFeatures enabled,
                                           BodyLocalDecls* decls,
                                           const WasmModule* module,
                                           bool is_shared, const uint8_t* start,
                                           const uint8_t* end, Zone* zone) {
  return DecodeLocalDecls<Decoder::BooleanValidationTag>(
      enabled, decls, module, is_shared, start, end, zone);
}

BytecodeIterator::BytecodeIterator(const uint8_t* start, const uint8_t* end)
    : Decoder(start, end) {}

BytecodeIterator::BytecodeIterator(const uint8_t* start, const uint8_t* end,
                                   BodyLocalDecls* decls, Zone* zone)
    : Decoder(start, end) {
  DCHECK_NOT_NULL(decls);
  DCHECK_NOT_NULL(zone);
  DecodeLocalDecls(WasmEnabledFeatures::All(), decls, start, end, zone);
  pc_ += decls->encoded_size;
  if (pc_ > end_) pc_ = end_;
}

DecodeResult ValidateFunctionBody(Zone* zone, WasmEnabledFeatures enabled,
                                  const WasmModule* module,
                                  WasmDetectedFeatures* detected,
                                  const FunctionBody& body) {
  // Asm.js functions should never be validated; they are valid by design.
  DCHECK_EQ(kWasmOrigin, module->origin);
  WasmFullDecoder<Decoder::FullValidationTag, EmptyInterface> decoder(
      zone, module, enabled, detected, body);
  decoder.Decode();
  return decoder.toResult(nullptr);
}

unsigned OpcodeLength(const uint8_t* pc, const uint8_t* end) {
  WasmDetectedFeatures unused_detected_features;
  Zone* no_zone = nullptr;
  WasmModule* no_module = nullptr;
  FunctionSig* no_sig = nullptr;
  constexpr bool kIsShared = false;
  WasmDecoder<Decoder::NoValidationTag> decoder(
      no_zone, no_module, WasmEnabledFeatures::All(), &unused_detected_features,
      no_sig, kIsShared, pc, end, 0);
  return WasmDecoder<Decoder::NoValidationTag>::OpcodeLength(&decoder, pc);
}

bool CheckHardwareSupportsSimd() { return CpuFeatures::SupportsWasmSimd128(); }

BitVector* AnalyzeLoopAssignmentForTesting(Zone* zone, uint32_t num_locals,
                                           const uint8_t* start,
                                           const uint8_t* end,
                                           bool* loop_is_innermost) {
  WasmEnabledFeatures no_features = WasmEnabledFeatures::None();
  WasmDetectedFeatures unused_detected_features;
  constexpr bool kIsShared = false;  // TODO(14616): Extend this.
  WasmDecoder<Decoder::FullValidationTag> decoder(
      zone, nullptr, no_features, &unused_detected_features, nullptr, kIsShared,
      start, end, 0);
  return WasmDecoder<Decoder::FullValidationTag>::AnalyzeLoopAssignment(
      &decoder, start, num_locals, zone, loop_is_innermost);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

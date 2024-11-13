// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MODULE_DECODER_H_
#define V8_WASM_MODULE_DECODER_H_

#include <memory>

#include "src/common/globals.h"
#include "src/logging/metrics.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {

class Counters;

namespace wasm {

struct CompilationEnv;

inline bool IsValidSectionCode(uint8_t byte) {
  // Allow everything within [kUnknownSectionCode, kLastKnownModuleSection].
  static_assert(kUnknownSectionCode == 0);
  return byte <= kLastKnownModuleSection;
}

V8_EXPORT_PRIVATE const char* SectionName(SectionCode code);

using ModuleResult = Result<std::shared_ptr<WasmModule>>;
using FunctionResult = Result<std::unique_ptr<WasmFunction>>;
using FunctionOffsets = std::vector<std::pair<int, int>>;
using FunctionOffsetsResult = Result<FunctionOffsets>;

struct AsmJsOffsetEntry {
  int byte_offset;
  int source_position_call;
  int source_position_number_conversion;
};
struct AsmJsOffsetFunctionEntries {
  int start_offset;
  int end_offset;
  std::vector<AsmJsOffsetEntry> entries;
};
struct AsmJsOffsets {
  std::vector<AsmJsOffsetFunctionEntries> functions;
};
using AsmJsOffsetsResult = Result<AsmJsOffsets>;

class DecodedNameSection {
 public:
  explicit DecodedNameSection(base::Vector<const uint8_t> wire_bytes,
                              WireBytesRef name_section);

 private:
  friend class NamesProvider;

  IndirectNameMap local_names_;
  IndirectNameMap label_names_;
  NameMap type_names_;
  NameMap table_names_;
  NameMap memory_names_;
  NameMap global_names_;
  NameMap element_segment_names_;
  NameMap data_segment_names_;
  IndirectNameMap field_names_;
  NameMap tag_names_;
};

enum class DecodingMethod {
  kSync,
  kAsync,
  kSyncStream,
  kAsyncStream,
  kDeserialize
};

// Decodes the bytes of a wasm module in {wire_bytes} while recording events and
// updating counters.
V8_EXPORT_PRIVATE ModuleResult DecodeWasmModule(
    WasmEnabledFeatures enabled_features,
    base::Vector<const uint8_t> wire_bytes, bool validate_functions,
    ModuleOrigin origin, Counters* counters,
    std::shared_ptr<metrics::Recorder> metrics_recorder,
    v8::metrics::Recorder::ContextId context_id,
    DecodingMethod decoding_method);
// Decodes the bytes of a wasm module in {wire_bytes} without recording events
// or updating counters.
V8_EXPORT_PRIVATE ModuleResult
DecodeWasmModule(WasmEnabledFeatures enabled_features,
                 base::Vector<const uint8_t> wire_bytes,
                 bool validate_functions, ModuleOrigin origin);
// Stripped down version for disassembler needs.
V8_EXPORT_PRIVATE ModuleResult DecodeWasmModuleForDisassembler(
    base::Vector<const uint8_t> wire_bytes, ITracer* tracer);

// Exposed for testing. Decodes a single function signature, allocating it
// in the given zone.
V8_EXPORT_PRIVATE Result<const FunctionSig*> DecodeWasmSignatureForTesting(
    WasmEnabledFeatures enabled_features, Zone* zone,
    base::Vector<const uint8_t> bytes);

// Decodes the bytes of a wasm function in {function_bytes} (part of
// {wire_bytes}).
V8_EXPORT_PRIVATE FunctionResult DecodeWasmFunctionForTesting(
    WasmEnabledFeatures enabled, Zone* zone, ModuleWireBytes wire_bytes,
    const WasmModule* module, base::Vector<const uint8_t> function_bytes);

V8_EXPORT_PRIVATE ConstantExpression DecodeWasmInitExprForTesting(
    WasmEnabledFeatures enabled_features, base::Vector<const uint8_t> bytes,
    ValueType expected);

struct CustomSectionOffset {
  WireBytesRef section;
  WireBytesRef name;
  WireBytesRef payload;
};

V8_EXPORT_PRIVATE std::vector<CustomSectionOffset> DecodeCustomSections(
    base::Vector<const uint8_t> wire_bytes);

// Extracts the mapping from wasm byte offset to asm.js source position per
// function.
AsmJsOffsetsResult DecodeAsmJsOffsets(
    base::Vector<const uint8_t> encoded_offsets);

// Decode the function names from the name section. Returns the result as an
// unordered map. Only names with valid utf8 encoding are stored and conflicts
// are resolved by choosing the last name read.
void DecodeFunctionNames(base::Vector<const uint8_t> wire_bytes,
                         NameMap& names);

// Validate specific functions in the module. Return the first validation error
// (deterministically), or an empty {WasmError} if all validated functions are
// valid. {filter} determines which functions are validated. Pass an empty
// function for "all functions". The {filter} callback needs to be thread-safe.
V8_EXPORT_PRIVATE WasmError ValidateFunctions(
    const WasmModule*, WasmEnabledFeatures enabled_features,
    base::Vector<const uint8_t> wire_bytes, std::function<bool(int)> filter);

WasmError GetWasmErrorWithName(base::Vector<const uint8_t> wire_bytes,
                               int func_index, const WasmModule* module,
                               WasmError error);

class ModuleDecoderImpl;

class ModuleDecoder {
 public:
  explicit ModuleDecoder(WasmEnabledFeatures enabled_feature);
  ~ModuleDecoder();

  void DecodeModuleHeader(base::Vector<const uint8_t> bytes);

  void DecodeSection(SectionCode section_code,
                     base::Vector<const uint8_t> bytes, uint32_t offset);

  void StartCodeSection(WireBytesRef section_bytes);

  bool CheckFunctionsCount(uint32_t functions_count, uint32_t error_offset);

  void DecodeFunctionBody(uint32_t index, uint32_t size, uint32_t offset);

  ModuleResult FinishDecoding();

  const std::shared_ptr<WasmModule>& shared_module() const;

  WasmModule* module() const { return shared_module().get(); }

  bool ok();

  // Translates the unknown section that decoder is pointing to to an extended
  // SectionCode if the unknown section is known to decoder.
  // The decoder is expected to point after the section length and just before
  // the identifier string of the unknown section.
  // The return value is the number of bytes that were consumed.
  static size_t IdentifyUnknownSection(ModuleDecoder* decoder,
                                       base::Vector<const uint8_t> bytes,
                                       uint32_t offset, SectionCode* result);

 private:
  std::unique_ptr<ModuleDecoderImpl> impl_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_DECODER_H_

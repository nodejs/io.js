// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/module-decoder.h"

#include "src/logging/metrics.h"
#include "src/tracing/trace-event.h"
#include "src/wasm/constant-expression.h"
#include "src/wasm/decoder.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

const char* SectionName(SectionCode code) {
  switch (code) {
    case kUnknownSectionCode:
      return "Unknown";
    case kTypeSectionCode:
      return "Type";
    case kImportSectionCode:
      return "Import";
    case kFunctionSectionCode:
      return "Function";
    case kTableSectionCode:
      return "Table";
    case kMemorySectionCode:
      return "Memory";
    case kGlobalSectionCode:
      return "Global";
    case kExportSectionCode:
      return "Export";
    case kStartSectionCode:
      return "Start";
    case kCodeSectionCode:
      return "Code";
    case kElementSectionCode:
      return "Element";
    case kDataSectionCode:
      return "Data";
    case kTagSectionCode:
      return "Tag";
    case kStringRefSectionCode:
      return "StringRef";
    case kDataCountSectionCode:
      return "DataCount";
    case kNameSectionCode:
      return kNameString;
    case kSourceMappingURLSectionCode:
      return kSourceMappingURLString;
    case kDebugInfoSectionCode:
      return kDebugInfoString;
    case kExternalDebugInfoSectionCode:
      return kExternalDebugInfoString;
    case kInstTraceSectionCode:
      return kInstTraceString;
    case kCompilationHintsSectionCode:
      return kCompilationHintsString;
    case kBranchHintsSectionCode:
      return kBranchHintsString;
    default:
      return "<unknown>";
  }
}

ModuleResult DecodeWasmModule(
    WasmEnabledFeatures enabled_features,
    base::Vector<const uint8_t> wire_bytes, bool validate_functions,
    ModuleOrigin origin, Counters* counters,
    std::shared_ptr<metrics::Recorder> metrics_recorder,
    v8::metrics::Recorder::ContextId context_id,
    DecodingMethod decoding_method) {
  if (counters) {
    auto size_counter =
        SELECT_WASM_COUNTER(counters, origin, wasm, module_size_bytes);
    static_assert(kV8MaxWasmModuleSize < kMaxInt);
    size_counter->AddSample(static_cast<int>(wire_bytes.size()));
  }

  v8::metrics::WasmModuleDecoded metrics_event;
  base::ElapsedTimer timer;
  timer.Start();
  ModuleResult result = DecodeWasmModule(enabled_features, wire_bytes,
                                         validate_functions, origin);
  if (counters && result.ok()) {
    auto counter =
        SELECT_WASM_COUNTER(counters, origin, wasm_functions_per, module);
    counter->AddSample(
        static_cast<int>(result.value()->num_declared_functions));
  }

  // Record event metrics.
  metrics_event.wall_clock_duration_in_us = timer.Elapsed().InMicroseconds();
  timer.Stop();
  metrics_event.success = result.ok();
  metrics_event.async = decoding_method == DecodingMethod::kAsync ||
                        decoding_method == DecodingMethod::kAsyncStream;
  metrics_event.streamed = decoding_method == DecodingMethod::kSyncStream ||
                           decoding_method == DecodingMethod::kAsyncStream;
  if (result.ok()) {
    metrics_event.function_count = result.value()->num_declared_functions;
  }
  metrics_event.module_size_in_bytes = wire_bytes.size();
  metrics_recorder->DelayMainThreadEvent(metrics_event, context_id);

  return result;
}

ModuleResult DecodeWasmModule(WasmEnabledFeatures enabled_features,
                              base::Vector<const uint8_t> wire_bytes,
                              bool validate_functions, ModuleOrigin origin) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.DecodeWasmModule");
  ModuleDecoderImpl decoder{enabled_features, wire_bytes, origin};
  return decoder.DecodeModule(validate_functions);
}

ModuleResult DecodeWasmModuleForDisassembler(
    base::Vector<const uint8_t> wire_bytes, ITracer* tracer) {
  constexpr bool kNoValidateFunctions = false;
  ModuleDecoderImpl decoder{WasmEnabledFeatures::All(), wire_bytes, kWasmOrigin,
                            tracer};
  return decoder.DecodeModule(kNoValidateFunctions);
}

ModuleDecoder::ModuleDecoder(WasmEnabledFeatures enabled_features)
    : impl_(std::make_unique<ModuleDecoderImpl>(
          enabled_features, base::Vector<const uint8_t>{}, kWasmOrigin)) {}

ModuleDecoder::~ModuleDecoder() = default;

const std::shared_ptr<WasmModule>& ModuleDecoder::shared_module() const {
  return impl_->shared_module();
}

void ModuleDecoder::DecodeModuleHeader(base::Vector<const uint8_t> bytes) {
  impl_->DecodeModuleHeader(bytes);
}

void ModuleDecoder::DecodeSection(SectionCode section_code,
                                  base::Vector<const uint8_t> bytes,
                                  uint32_t offset) {
  impl_->DecodeSection(section_code, bytes, offset);
}

void ModuleDecoder::DecodeFunctionBody(uint32_t index, uint32_t length,
                                       uint32_t offset) {
  impl_->DecodeFunctionBody(index, length, offset);
}

void ModuleDecoder::StartCodeSection(WireBytesRef section_bytes) {
  impl_->StartCodeSection(section_bytes);
}

bool ModuleDecoder::CheckFunctionsCount(uint32_t functions_count,
                                        uint32_t error_offset) {
  return impl_->CheckFunctionsCount(functions_count, error_offset);
}

ModuleResult ModuleDecoder::FinishDecoding() { return impl_->FinishDecoding(); }

size_t ModuleDecoder::IdentifyUnknownSection(ModuleDecoder* decoder,
                                             base::Vector<const uint8_t> bytes,
                                             uint32_t offset,
                                             SectionCode* result) {
  if (!decoder->ok()) return 0;
  decoder->impl_->Reset(bytes, offset);
  *result =
      IdentifyUnknownSectionInternal(decoder->impl_.get(), ITracer::NoTrace);
  return decoder->impl_->pc() - bytes.begin();
}

bool ModuleDecoder::ok() { return impl_->ok(); }

Result<const FunctionSig*> DecodeWasmSignatureForTesting(
    WasmEnabledFeatures enabled_features, Zone* zone,
    base::Vector<const uint8_t> bytes) {
  ModuleDecoderImpl decoder{enabled_features, bytes, kWasmOrigin};
  return decoder.toResult(decoder.DecodeFunctionSignature(zone, bytes.begin()));
}

ConstantExpression DecodeWasmInitExprForTesting(
    WasmEnabledFeatures enabled_features, base::Vector<const uint8_t> bytes,
    ValueType expected) {
  ModuleDecoderImpl decoder{enabled_features, bytes, kWasmOrigin};
  return decoder.DecodeInitExprForTesting(expected);
}

FunctionResult DecodeWasmFunctionForTesting(
    WasmEnabledFeatures enabled_features, Zone* zone,
    ModuleWireBytes wire_bytes, const WasmModule* module,
    base::Vector<const uint8_t> function_bytes) {
  if (function_bytes.size() > kV8MaxWasmFunctionSize) {
    return FunctionResult{
        WasmError{0, "size > maximum function size (%zu): %zu",
                  kV8MaxWasmFunctionSize, function_bytes.size()}};
  }
  ModuleDecoderImpl decoder{enabled_features, function_bytes, kWasmOrigin};
  return decoder.DecodeSingleFunctionForTesting(zone, wire_bytes, module);
}

AsmJsOffsetsResult DecodeAsmJsOffsets(
    base::Vector<const uint8_t> encoded_offsets) {
  std::vector<AsmJsOffsetFunctionEntries> functions;

  Decoder decoder(encoded_offsets);
  uint32_t functions_count = decoder.consume_u32v("functions count");
  // Consistency check.
  DCHECK_GE(encoded_offsets.size(), functions_count);
  functions.reserve(functions_count);

  for (uint32_t i = 0; i < functions_count; ++i) {
    uint32_t size = decoder.consume_u32v("table size");
    if (size == 0) {
      functions.emplace_back();
      continue;
    }
    DCHECK(decoder.checkAvailable(size));
    const uint8_t* table_end = decoder.pc() + size;
    uint32_t locals_size = decoder.consume_u32v("locals size");
    int function_start_position = decoder.consume_u32v("function start pos");
    int function_end_position = function_start_position;
    int last_byte_offset = locals_size;
    int last_asm_position = function_start_position;
    std::vector<AsmJsOffsetEntry> func_asm_offsets;
    func_asm_offsets.reserve(size / 4);  // conservative estimation
    // Add an entry for the stack check, associated with position 0.
    func_asm_offsets.push_back(
        {0, function_start_position, function_start_position});
    while (decoder.pc() < table_end) {
      DCHECK(decoder.ok());
      last_byte_offset += decoder.consume_u32v("byte offset delta");
      int call_position =
          last_asm_position + decoder.consume_i32v("call position delta");
      int to_number_position =
          call_position + decoder.consume_i32v("to_number position delta");
      last_asm_position = to_number_position;
      if (decoder.pc() == table_end) {
        // The last entry is the function end marker.
        DCHECK_EQ(call_position, to_number_position);
        function_end_position = call_position;
      } else {
        func_asm_offsets.push_back(
            {last_byte_offset, call_position, to_number_position});
      }
    }
    DCHECK_EQ(decoder.pc(), table_end);
    functions.emplace_back(AsmJsOffsetFunctionEntries{
        function_start_position, function_end_position,
        std::move(func_asm_offsets)});
  }
  DCHECK(decoder.ok());
  DCHECK(!decoder.more());

  return decoder.toResult(AsmJsOffsets{std::move(functions)});
}

std::vector<CustomSectionOffset> DecodeCustomSections(
    base::Vector<const uint8_t> bytes) {
  Decoder decoder(bytes);
  decoder.consume_bytes(4, "wasm magic");
  decoder.consume_bytes(4, "wasm version");

  std::vector<CustomSectionOffset> result;

  while (decoder.more()) {
    uint8_t section_code = decoder.consume_u8("section code");
    uint32_t section_length = decoder.consume_u32v("section length");
    uint32_t section_start = decoder.pc_offset();
    if (section_code != 0) {
      // Skip known sections.
      decoder.consume_bytes(section_length, "section bytes");
      continue;
    }
    uint32_t name_length = decoder.consume_u32v("name length");
    uint32_t name_offset = decoder.pc_offset();
    decoder.consume_bytes(name_length, "section name");
    uint32_t payload_offset = decoder.pc_offset();
    if (section_length < (payload_offset - section_start)) {
      decoder.error("invalid section length");
      break;
    }
    uint32_t payload_length = section_length - (payload_offset - section_start);
    decoder.consume_bytes(payload_length);
    if (decoder.failed()) break;
    result.push_back({{section_start, section_length},
                      {name_offset, name_length},
                      {payload_offset, payload_length}});
  }

  return result;
}

namespace {

bool FindNameSection(Decoder* decoder) {
  static constexpr int kModuleHeaderSize = 8;
  decoder->consume_bytes(kModuleHeaderSize, "module header");

  WasmSectionIterator section_iter(decoder, ITracer::NoTrace);

  while (decoder->ok() && section_iter.more() &&
         section_iter.section_code() != kNameSectionCode) {
    section_iter.advance(true);
  }
  if (!section_iter.more()) return false;

  // Reset the decoder to not read beyond the name section end.
  decoder->Reset(section_iter.payload(), decoder->pc_offset());
  return true;
}

enum class EmptyNames : bool { kAllow, kSkip };

void DecodeNameMapInternal(NameMap& target, Decoder& decoder,
                           EmptyNames empty_names = EmptyNames::kSkip) {
  uint32_t count = decoder.consume_u32v("names count");
  for (uint32_t i = 0; i < count; i++) {
    uint32_t index = decoder.consume_u32v("index");
    WireBytesRef name =
        consume_string(&decoder, unibrow::Utf8Variant::kLossyUtf8, "name");
    if (!decoder.ok()) break;
    if (index > NameMap::kMaxKey) continue;
    if (empty_names == EmptyNames::kSkip && name.is_empty()) continue;
    if (!validate_utf8(&decoder, name)) continue;
    target.Put(index, name);
  }
  target.FinishInitialization();
}

void DecodeNameMap(NameMap& target, Decoder& decoder,
                   uint32_t subsection_payload_length,
                   EmptyNames empty_names = EmptyNames::kSkip) {
  if (target.is_set()) {
    decoder.consume_bytes(subsection_payload_length);
    return;
  }
  DecodeNameMapInternal(target, decoder, empty_names);
}

void DecodeIndirectNameMap(IndirectNameMap& target, Decoder& decoder,
                           uint32_t subsection_payload_length) {
  if (target.is_set()) {
    decoder.consume_bytes(subsection_payload_length);
    return;
  }
  uint32_t outer_count = decoder.consume_u32v("outer count");
  for (uint32_t i = 0; i < outer_count; ++i) {
    uint32_t outer_index = decoder.consume_u32v("outer index");
    if (outer_index > IndirectNameMap::kMaxKey) continue;
    NameMap names;
    DecodeNameMapInternal(names, decoder);
    target.Put(outer_index, std::move(names));
    if (!decoder.ok()) break;
  }
  target.FinishInitialization();
}

}  // namespace

void DecodeFunctionNames(base::Vector<const uint8_t> wire_bytes,
                         NameMap& names) {
  Decoder decoder(wire_bytes);
  if (FindNameSection(&decoder)) {
    while (decoder.ok() && decoder.more()) {
      uint8_t name_type = decoder.consume_u8("name type");
      if (name_type & 0x80) break;  // no varuint7

      uint32_t name_payload_len = decoder.consume_u32v("name payload length");
      if (!decoder.checkAvailable(name_payload_len)) break;

      if (name_type != NameSectionKindCode::kFunctionCode) {
        decoder.consume_bytes(name_payload_len, "name subsection payload");
        continue;
      }
      // We need to allow empty function names for spec-conformant stack traces.
      DecodeNameMapInternal(names, decoder, EmptyNames::kAllow);
      // The spec allows only one occurrence of each subsection. We could be
      // more permissive and allow repeated subsections; in that case we'd
      // have to delay calling {target.FinishInitialization()} on the function
      // names map until we've seen them all.
      // For now, we stop decoding after finding the first function names
      // subsection.
      return;
    }
  }
}

namespace {
// A task that validates multiple functions in parallel, storing the earliest
// validation error in {this} decoder.
class ValidateFunctionsTask : public JobTask {
 public:
  explicit ValidateFunctionsTask(base::Vector<const uint8_t> wire_bytes,
                                 const WasmModule* module,
                                 WasmEnabledFeatures enabled_features,
                                 std::function<bool(int)> filter,
                                 WasmError* error_out)
      : wire_bytes_(wire_bytes),
        module_(module),
        enabled_features_(enabled_features),
        filter_(std::move(filter)),
        next_function_(module->num_imported_functions),
        after_last_function_(next_function_ + module->num_declared_functions),
        error_out_(error_out) {
    DCHECK(!error_out->has_error());
  }

  void Run(JobDelegate* delegate) override {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
                 "wasm.ValidateFunctionsTask");

    Zone zone(GetWasmEngine()->allocator(), ZONE_NAME);
    do {
      // Get the index of the next function to validate.
      // {fetch_add} might overrun {after_last_function_} by a bit. Since the
      // number of functions is limited to a value much smaller than the
      // integer range, this is near impossible to happen.
      static_assert(kV8MaxWasmTotalFunctions < kMaxInt / 2);
      int func_index;
      do {
        func_index = next_function_.fetch_add(1, std::memory_order_relaxed);
        if (V8_UNLIKELY(func_index >= after_last_function_)) return;
        DCHECK_LE(0, func_index);
      } while ((filter_ && !filter_(func_index)) ||
               module_->function_was_validated(func_index));

      zone.Reset();
      if (!ValidateFunction(func_index, &zone)) {
        // No need to validate any more functions.
        next_function_.store(after_last_function_, std::memory_order_relaxed);
        return;
      }
    } while (!delegate->ShouldYield());
  }

  size_t GetMaxConcurrency(size_t /* worker_count */) const override {
    int next_func = next_function_.load(std::memory_order_relaxed);
    return std::max(0, after_last_function_ - next_func);
  }

 private:
  bool ValidateFunction(int func_index, Zone* zone) {
    WasmDetectedFeatures unused_detected_features;
    const WasmFunction& function = module_->functions[func_index];
    DCHECK_LT(0, function.code.offset());
    bool is_shared = module_->types[function.sig_index].is_shared;
    FunctionBody body{function.sig, function.code.offset(),
                      wire_bytes_.begin() + function.code.offset(),
                      wire_bytes_.begin() + function.code.end_offset(),
                      is_shared};
    DecodeResult validation_result = ValidateFunctionBody(
        zone, enabled_features_, module_, &unused_detected_features, body);
    if (V8_UNLIKELY(validation_result.failed())) {
      SetError(func_index, std::move(validation_result).error());
      return false;
    }
    module_->set_function_validated(func_index);
    return true;
  }

  // Set the error from the argument if it's earlier than the error we already
  // have (or if we have none yet). Thread-safe.
  void SetError(int func_index, WasmError error) {
    base::MutexGuard mutex_guard{&set_error_mutex_};
    if (error_out_->has_error() && error_out_->offset() <= error.offset()) {
      return;
    }
    *error_out_ = GetWasmErrorWithName(wire_bytes_, func_index, module_, error);
  }

  const base::Vector<const uint8_t> wire_bytes_;
  const WasmModule* const module_;
  const WasmEnabledFeatures enabled_features_;
  const std::function<bool(int)> filter_;
  std::atomic<int> next_function_;
  const int after_last_function_;
  base::Mutex set_error_mutex_;
  WasmError* const error_out_;
};
}  // namespace

WasmError ValidateFunctions(const WasmModule* module,
                            WasmEnabledFeatures enabled_features,
                            base::Vector<const uint8_t> wire_bytes,
                            std::function<bool(int)> filter) {
  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.ValidateFunctions", "num_declared_functions",
               module->num_declared_functions, "has_filter", filter != nullptr);
  DCHECK_EQ(kWasmOrigin, module->origin);

  class NeverYieldDelegate final : public JobDelegate {
   public:
    bool ShouldYield() override { return false; }

    bool IsJoiningThread() const override { UNIMPLEMENTED(); }
    void NotifyConcurrencyIncrease() override { UNIMPLEMENTED(); }
    uint8_t GetTaskId() override { UNIMPLEMENTED(); }
  };

  // Create a {ValidateFunctionsTask} to validate all functions. The earliest
  // error found will be set on this decoder.
  WasmError validation_error;
  std::unique_ptr<JobTask> validate_job =
      std::make_unique<ValidateFunctionsTask>(
          wire_bytes, module, enabled_features, std::move(filter),
          &validation_error);

  if (v8_flags.single_threaded) {
    // In single-threaded mode, run the {ValidateFunctionsTask} synchronously.
    NeverYieldDelegate delegate;
    validate_job->Run(&delegate);
  } else {
    // Spawn the task and join it.
    std::unique_ptr<JobHandle> job_handle = V8::GetCurrentPlatform()->CreateJob(
        TaskPriority::kUserVisible, std::move(validate_job));
    job_handle->Join();
  }

  return validation_error;
}

WasmError GetWasmErrorWithName(base::Vector<const uint8_t> wire_bytes,
                               int func_index, const WasmModule* module,
                               WasmError error) {
  WasmName name = ModuleWireBytes{wire_bytes}.GetNameOrNull(func_index, module);
  if (name.begin() == nullptr) {
    return WasmError(error.offset(), "Compiling function #%d failed: %s",
                     func_index, error.message().c_str());
  } else {
    TruncatedUserString<> truncated_name(name);
    return WasmError(error.offset(),
                     "Compiling function #%d:\"%.*s\" failed: %s", func_index,
                     truncated_name.length(), truncated_name.start(),
                     error.message().c_str());
  }
}

DecodedNameSection::DecodedNameSection(base::Vector<const uint8_t> wire_bytes,
                                       WireBytesRef name_section) {
  if (name_section.is_empty()) return;  // No name section.
  Decoder decoder(wire_bytes.begin() + name_section.offset(),
                  wire_bytes.begin() + name_section.end_offset(),
                  name_section.offset());
  while (decoder.ok() && decoder.more()) {
    uint8_t name_type = decoder.consume_u8("name type");
    if (name_type & 0x80) break;  // no varuint7

    uint32_t name_payload_len = decoder.consume_u32v("name payload length");
    if (!decoder.checkAvailable(name_payload_len)) break;

    switch (name_type) {
      case kModuleCode:
      case kFunctionCode:
        // Already handled elsewhere.
        decoder.consume_bytes(name_payload_len);
        break;
      case kLocalCode:
        static_assert(kV8MaxWasmTotalFunctions <= IndirectNameMap::kMaxKey);
        static_assert(kV8MaxWasmFunctionLocals <= NameMap::kMaxKey);
        DecodeIndirectNameMap(local_names_, decoder, name_payload_len);
        break;
      case kLabelCode:
        static_assert(kV8MaxWasmTotalFunctions <= IndirectNameMap::kMaxKey);
        static_assert(kV8MaxWasmFunctionSize <= NameMap::kMaxKey);
        DecodeIndirectNameMap(label_names_, decoder, name_payload_len);
        break;
      case kTypeCode:
        static_assert(kV8MaxWasmTypes <= NameMap::kMaxKey);
        DecodeNameMap(type_names_, decoder, name_payload_len);
        break;
      case kTableCode:
        static_assert(kV8MaxWasmTables <= NameMap::kMaxKey);
        DecodeNameMap(table_names_, decoder, name_payload_len);
        break;
      case kMemoryCode:
        static_assert(kV8MaxWasmMemories <= NameMap::kMaxKey);
        DecodeNameMap(memory_names_, decoder, name_payload_len);
        break;
      case kGlobalCode:
        static_assert(kV8MaxWasmGlobals <= NameMap::kMaxKey);
        DecodeNameMap(global_names_, decoder, name_payload_len);
        break;
      case kElementSegmentCode:
        static_assert(kV8MaxWasmTableInitEntries <= NameMap::kMaxKey);
        DecodeNameMap(element_segment_names_, decoder, name_payload_len);
        break;
      case kDataSegmentCode:
        static_assert(kV8MaxWasmDataSegments <= NameMap::kMaxKey);
        DecodeNameMap(data_segment_names_, decoder, name_payload_len);
        break;
      case kFieldCode:
        static_assert(kV8MaxWasmTypes <= IndirectNameMap::kMaxKey);
        static_assert(kV8MaxWasmStructFields <= NameMap::kMaxKey);
        DecodeIndirectNameMap(field_names_, decoder, name_payload_len);
        break;
      case kTagCode:
        static_assert(kV8MaxWasmTags <= NameMap::kMaxKey);
        DecodeNameMap(tag_names_, decoder, name_payload_len);
        break;
    }
  }
}

#undef TRACE

}  // namespace wasm
}  // namespace internal
}  // namespace v8

// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/asmjs/asm-js.h"

#include "src/asmjs/asm-names.h"
#include "src/asmjs/asm-parser.h"
#include "src/ast/ast.h"
#include "src/base/optional.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/vector.h"
#include "src/codegen/compiler.h"
#include "src/codegen/unoptimized-compilation-info.h"
#include "src/common/assert-scope.h"
#include "src/common/message-template.h"
#include "src/execution/execution.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/logging/counters.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/scanner.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {

const char* const AsmJs::kSingleFunctionName = "__single_function__";

namespace {

Handle<Object> StdlibMathMember(Isolate* isolate, Handle<JSReceiver> stdlib,
                                Handle<Name> name) {
  Handle<Name> math_name(
      isolate->factory()->InternalizeString(base::StaticCharVector("Math")));
  Handle<Object> math = JSReceiver::GetDataProperty(isolate, stdlib, math_name);
  if (!IsJSReceiver(*math)) return isolate->factory()->undefined_value();
  Handle<JSReceiver> math_receiver = Handle<JSReceiver>::cast(math);
  Handle<Object> value =
      JSReceiver::GetDataProperty(isolate, math_receiver, name);
  return value;
}

bool AreStdlibMembersValid(Isolate* isolate, Handle<JSReceiver> stdlib,
                           wasm::AsmJsParser::StdlibSet members,
                           bool* is_typed_array) {
  if (members.contains(wasm::AsmJsParser::StandardMember::kInfinity)) {
    members.Remove(wasm::AsmJsParser::StandardMember::kInfinity);
    Handle<Name> name = isolate->factory()->Infinity_string();
    Handle<Object> value = JSReceiver::GetDataProperty(isolate, stdlib, name);
    if (!IsNumber(*value) || !std::isinf(Object::Number(*value))) return false;
  }
  if (members.contains(wasm::AsmJsParser::StandardMember::kNaN)) {
    members.Remove(wasm::AsmJsParser::StandardMember::kNaN);
    Handle<Name> name = isolate->factory()->NaN_string();
    Handle<Object> value = JSReceiver::GetDataProperty(isolate, stdlib, name);
    if (!IsNaN(*value)) return false;
  }
#define STDLIB_MATH_FUNC(fname, FName, ignore1, ignore2)                   \
  if (members.contains(wasm::AsmJsParser::StandardMember::kMath##FName)) { \
    members.Remove(wasm::AsmJsParser::StandardMember::kMath##FName);       \
    Handle<Name> name(isolate->factory()->InternalizeString(               \
        base::StaticCharVector(#fname)));                                  \
    Handle<Object> value = StdlibMathMember(isolate, stdlib, name);        \
    if (!IsJSFunction(*value)) return false;                               \
    Tagged<SharedFunctionInfo> shared =                                    \
        Handle<JSFunction>::cast(value)->shared();                         \
    if (!shared->HasBuiltinId() ||                                         \
        shared->builtin_id() != Builtin::kMath##FName) {                   \
      return false;                                                        \
    }                                                                      \
    DCHECK_EQ(shared->GetCode(isolate),                                    \
              isolate->builtins()->code(Builtin::kMath##FName));           \
  }
  STDLIB_MATH_FUNCTION_LIST(STDLIB_MATH_FUNC)
#undef STDLIB_MATH_FUNC
#define STDLIB_MATH_CONST(cname, const_value)                              \
  if (members.contains(wasm::AsmJsParser::StandardMember::kMath##cname)) { \
    members.Remove(wasm::AsmJsParser::StandardMember::kMath##cname);       \
    Handle<Name> name(isolate->factory()->InternalizeString(               \
        base::StaticCharVector(#cname)));                                  \
    Handle<Object> value = StdlibMathMember(isolate, stdlib, name);        \
    if (!IsNumber(*value) || Object::Number(*value) != const_value)        \
      return false;                                                        \
  }
  STDLIB_MATH_VALUE_LIST(STDLIB_MATH_CONST)
#undef STDLIB_MATH_CONST
#define STDLIB_ARRAY_TYPE(fname, FName)                                        \
  if (members.contains(wasm::AsmJsParser::StandardMember::k##FName)) {         \
    members.Remove(wasm::AsmJsParser::StandardMember::k##FName);               \
    *is_typed_array = true;                                                    \
    Handle<Name> name(isolate->factory()->InternalizeString(                   \
        base::StaticCharVector(#FName)));                                      \
    Handle<Object> value = JSReceiver::GetDataProperty(isolate, stdlib, name); \
    if (!IsJSFunction(*value)) return false;                                   \
    Handle<JSFunction> func = Handle<JSFunction>::cast(value);                 \
    if (!func.is_identical_to(isolate->fname())) return false;                 \
  }
  STDLIB_ARRAY_TYPE(int8_array_fun, Int8Array)
  STDLIB_ARRAY_TYPE(uint8_array_fun, Uint8Array)
  STDLIB_ARRAY_TYPE(int16_array_fun, Int16Array)
  STDLIB_ARRAY_TYPE(uint16_array_fun, Uint16Array)
  STDLIB_ARRAY_TYPE(int32_array_fun, Int32Array)
  STDLIB_ARRAY_TYPE(uint32_array_fun, Uint32Array)
  STDLIB_ARRAY_TYPE(float32_array_fun, Float32Array)
  STDLIB_ARRAY_TYPE(float64_array_fun, Float64Array)
#undef STDLIB_ARRAY_TYPE
  // All members accounted for.
  DCHECK(members.empty());
  return true;
}

void Report(Handle<Script> script, int position, base::Vector<const char> text,
            MessageTemplate message_template,
            v8::Isolate::MessageErrorLevel level) {
  Isolate* isolate = script->GetIsolate();
  MessageLocation location(script, position, position);
  Handle<String> text_object = isolate->factory()->InternalizeUtf8String(text);
  Handle<JSMessageObject> message = MessageHandler::MakeMessageObject(
      isolate, message_template, &location, text_object,
      Handle<FixedArray>::null());
  message->set_error_level(level);
  MessageHandler::ReportMessage(isolate, &location, message);
}

// Hook to report successful execution of {AsmJs::CompileAsmViaWasm} phase.
void ReportCompilationSuccess(Handle<Script> script, int position,
                              double compile_time, size_t module_size) {
  if (v8_flags.suppress_asm_messages || !v8_flags.trace_asm_time) return;
  base::EmbeddedVector<char, 100> text;
  int length = SNPrintF(text, "success, compile time %0.3f ms, %zu bytes",
                        compile_time, module_size);
  CHECK_NE(-1, length);
  text.Truncate(length);
  Report(script, position, text, MessageTemplate::kAsmJsCompiled,
         v8::Isolate::kMessageInfo);
}

// Hook to report failed execution of {AsmJs::CompileAsmViaWasm} phase.
void ReportCompilationFailure(ParseInfo* parse_info, int position,
                              const char* reason) {
  if (v8_flags.suppress_asm_messages) return;
  parse_info->pending_error_handler()->ReportWarningAt(
      position, position, MessageTemplate::kAsmJsInvalid, reason);
}

// Hook to report successful execution of {AsmJs::InstantiateAsmWasm} phase.
void ReportInstantiationSuccess(Handle<Script> script, int position,
                                double instantiate_time) {
  if (v8_flags.suppress_asm_messages || !v8_flags.trace_asm_time) return;
  base::EmbeddedVector<char, 50> text;
  int length = SNPrintF(text, "success, %0.3f ms", instantiate_time);
  CHECK_NE(-1, length);
  text.Truncate(length);
  Report(script, position, text, MessageTemplate::kAsmJsInstantiated,
         v8::Isolate::kMessageInfo);
}

// Hook to report failed execution of {AsmJs::InstantiateAsmWasm} phase.
void ReportInstantiationFailure(Handle<Script> script, int position,
                                const char* reason) {
  if (v8_flags.suppress_asm_messages) return;
  base::Vector<const char> text = base::CStrVector(reason);
  Report(script, position, text, MessageTemplate::kAsmJsLinkingFailed,
         v8::Isolate::kMessageWarning);
}

}  // namespace

// The compilation of asm.js modules is split into two distinct steps:
//  [1] ExecuteJobImpl: The asm.js module source is parsed, validated, and
//      translated to a valid WebAssembly module. The result are two vectors
//      representing the encoded module as well as encoded source position
//      information and a StdlibSet bit set.
//  [2] FinalizeJobImpl: The module is handed to WebAssembly which decodes it
//      into an internal representation and eventually compiles it to machine
//      code.
class AsmJsCompilationJob final : public UnoptimizedCompilationJob {
 public:
  explicit AsmJsCompilationJob(ParseInfo* parse_info, FunctionLiteral* literal,
                               AccountingAllocator* allocator)
      : UnoptimizedCompilationJob(parse_info->stack_limit(), parse_info,
                                  &compilation_info_),
        allocator_(allocator),
        zone_(allocator, ZONE_NAME),
        compilation_info_(&zone_, parse_info, literal),
        module_(nullptr),
        asm_offsets_(nullptr),
        compile_time_(0),
        module_source_size_(0) {}

  AsmJsCompilationJob(const AsmJsCompilationJob&) = delete;
  AsmJsCompilationJob& operator=(const AsmJsCompilationJob&) = delete;

 protected:
  Status ExecuteJobImpl() final;
  Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                         Isolate* isolate) final;
  Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                         LocalIsolate* isolate) final {
    return CompilationJob::RETRY_ON_MAIN_THREAD;
  }

 private:
  void RecordHistograms(Isolate* isolate);

  AccountingAllocator* allocator_;
  Zone zone_;
  UnoptimizedCompilationInfo compilation_info_;
  wasm::ZoneBuffer* module_;
  wasm::ZoneBuffer* asm_offsets_;
  wasm::AsmJsParser::StdlibSet stdlib_uses_;

  double compile_time_;     // Time (milliseconds) taken to execute step [2].
  int module_source_size_;  // Module source size in bytes.
};

UnoptimizedCompilationJob::Status AsmJsCompilationJob::ExecuteJobImpl() {
  DisallowHeapAccess no_heap_access;

  // Step 1: Translate asm.js module to WebAssembly module.
  Zone* compile_zone = &zone_;
  Zone translate_zone(allocator_, ZONE_NAME);

  Utf16CharacterStream* stream = parse_info()->character_stream();
  base::Optional<AllowHandleDereference> allow_deref;
  if (stream->can_access_heap()) {
    allow_deref.emplace();
  }
  stream->Seek(compilation_info()->literal()->start_position());
  wasm::AsmJsParser parser(&translate_zone, stack_limit(), stream);
  if (!parser.Run()) {
    if (!v8_flags.suppress_asm_messages) {
      ReportCompilationFailure(parse_info(), parser.failure_location(),
                               parser.failure_message());
    }
    return FAILED;
  }
  module_ = compile_zone->New<wasm::ZoneBuffer>(compile_zone);
  parser.module_builder()->WriteTo(module_);
  asm_offsets_ = compile_zone->New<wasm::ZoneBuffer>(compile_zone);
  parser.module_builder()->WriteAsmJsOffsetTable(asm_offsets_);
  stdlib_uses_ = *parser.stdlib_uses();

  module_source_size_ = compilation_info()->literal()->end_position() -
                        compilation_info()->literal()->start_position();
  return SUCCEEDED;
}

UnoptimizedCompilationJob::Status AsmJsCompilationJob::FinalizeJobImpl(
    Handle<SharedFunctionInfo> shared_info, Isolate* isolate) {
  // Step 2: Compile and decode the WebAssembly module.
  base::ElapsedTimer compile_timer;
  compile_timer.Start();

  Handle<HeapNumber> uses_bitset =
      isolate->factory()->NewHeapNumberFromBits(stdlib_uses_.ToIntegral());

  // The result is a compiled module and serialized standard library uses.
  wasm::ErrorThrower thrower(isolate, "AsmJs::Compile");
  Handle<Script> script(Script::cast(shared_info->script()), isolate);
  Handle<AsmWasmData> result =
      wasm::GetWasmEngine()
          ->SyncCompileTranslatedAsmJs(
              isolate, &thrower,
              wasm::ModuleWireBytes(module_->begin(), module_->end()), script,
              base::VectorOf(*asm_offsets_), uses_bitset,
              shared_info->language_mode())
          .ToHandleChecked();
  DCHECK(!thrower.error());
  compile_time_ = compile_timer.Elapsed().InMillisecondsF();

  compilation_info()->SetAsmWasmData(result);

  RecordHistograms(isolate);
  ReportCompilationSuccess(script, shared_info->StartPosition(), compile_time_,
                           module_->size());
  return SUCCEEDED;
}

void AsmJsCompilationJob::RecordHistograms(Isolate* isolate) {
  isolate->counters()->asm_module_size_bytes()->AddSample(module_source_size_);
}

std::unique_ptr<UnoptimizedCompilationJob> AsmJs::NewCompilationJob(
    ParseInfo* parse_info, FunctionLiteral* literal,
    AccountingAllocator* allocator) {
  return std::make_unique<AsmJsCompilationJob>(parse_info, literal, allocator);
}

namespace {
inline bool IsValidAsmjsMemorySize(size_t size) {
  // Enforce asm.js spec minimum size.
  if (size < (1u << 12u)) return false;
  // Enforce engine-limited and flag-limited maximum allocation size.
  if (size > wasm::max_mem32_bytes()) return false;
  // Enforce power-of-2 sizes for 2^12 - 2^24.
  if (size < (1u << 24u)) {
    uint32_t size32 = static_cast<uint32_t>(size);
    return base::bits::IsPowerOfTwo(size32);
  }
  // Enforce multiple of 2^24 for sizes >= 2^24
  if ((size % (1u << 24u)) != 0) return false;
  // Limitation of our implementation: for performance reasons, we use unsigned
  // uint32-to-uintptr extensions for memory addresses, which would give
  // incorrect behavior for memories larger than 2 GiB.
  // Note that this does not affect Chrome, which does not allow allocating
  // larger ArrayBuffers anyway.
  if (size > 0x8000'0000u) return false;
  // All checks passed!
  return true;
}
}  // namespace

MaybeHandle<Object> AsmJs::InstantiateAsmWasm(Isolate* isolate,
                                              Handle<SharedFunctionInfo> shared,
                                              Handle<AsmWasmData> wasm_data,
                                              Handle<JSReceiver> stdlib,
                                              Handle<JSReceiver> foreign,
                                              Handle<JSArrayBuffer> memory) {
  base::ElapsedTimer instantiate_timer;
  instantiate_timer.Start();
  Handle<HeapNumber> uses_bitset(wasm_data->uses_bitset(), isolate);
  Handle<Script> script(Script::cast(shared->script()), isolate);
  auto* wasm_engine = wasm::GetWasmEngine();

  // Allocate the WasmModuleObject.
  Handle<WasmModuleObject> module =
      wasm_engine->FinalizeTranslatedAsmJs(isolate, wasm_data, script);

  // TODO(asmjs): The position currently points to the module definition
  // but should instead point to the instantiation site (more intuitive).
  int position = shared->StartPosition();

  // Check that the module is not instantiated as a generator or async function.
  if (IsResumableFunction(shared->scope_info()->function_kind())) {
    ReportInstantiationFailure(script, position,
                               "Cannot be instantiated as resumable function");
    return MaybeHandle<Object>();
  }

  // Check that all used stdlib members are valid.
  bool stdlib_use_of_typed_array_present = false;
  wasm::AsmJsParser::StdlibSet stdlib_uses =
      wasm::AsmJsParser::StdlibSet::FromIntegral(uses_bitset->value_as_bits());
  if (!stdlib_uses.empty()) {  // No checking needed if no uses.
    if (stdlib.is_null()) {
      ReportInstantiationFailure(script, position, "Requires standard library");
      return MaybeHandle<Object>();
    }
    if (!AreStdlibMembersValid(isolate, stdlib, stdlib_uses,
                               &stdlib_use_of_typed_array_present)) {
      ReportInstantiationFailure(script, position, "Unexpected stdlib member");
      return MaybeHandle<Object>();
    }
  }

  // Check that a valid heap buffer is provided if required.
  if (stdlib_use_of_typed_array_present) {
    if (memory.is_null()) {
      ReportInstantiationFailure(script, position, "Requires heap buffer");
      return MaybeHandle<Object>();
    }
    // AsmJs memory must be an ArrayBuffer.
    if (memory->is_shared()) {
      ReportInstantiationFailure(script, position,
                                 "Invalid heap type: SharedArrayBuffer");
      return MaybeHandle<Object>();
    }
    // We don't allow resizable ArrayBuffers because resizable ArrayBuffers may
    // shrink, and then asm.js does out of bounds memory accesses.
    if (memory->is_resizable_by_js()) {
      ReportInstantiationFailure(script, position,
                                 "Invalid heap type: resizable ArrayBuffer");
      return MaybeHandle<Object>();
    }
    // We don't allow WebAssembly.Memory, because WebAssembly.Memory.grow()
    // detaches the ArrayBuffer, and that would invalidate the asm.js module.
    if (memory->GetBackingStore() &&
        memory->GetBackingStore()->is_wasm_memory()) {
      ReportInstantiationFailure(script, position,
                                 "Invalid heap type: WebAssembly.Memory");
      return MaybeHandle<Object>();
    }
    size_t size = memory->byte_length();
    // Check the asm.js heap size against the valid limits.
    if (!IsValidAsmjsMemorySize(size)) {
      ReportInstantiationFailure(script, position, "Invalid heap size");
      return MaybeHandle<Object>();
    }
    // Mark the buffer as undetachable. This implies that the buffer cannot be
    // postMessage()'d, as that detaches the buffer.
    memory->set_is_detachable(false);
  } else {
    memory = Handle<JSArrayBuffer>::null();
  }

  wasm::ErrorThrower thrower(isolate, "AsmJs::Instantiate");
  MaybeHandle<WasmInstanceObject> maybe_instance =
      wasm_engine->SyncInstantiate(isolate, &thrower, module, foreign, memory);
  if (maybe_instance.is_null()) {
    // Clear a possible stack overflow from function entry that would have
    // bypassed the {ErrorThrower}. Be careful not to clear a termination
    // exception.
    if (isolate->is_execution_terminating()) return {};
    if (isolate->has_exception()) isolate->clear_exception();
    if (thrower.error()) {
      base::ScopedVector<char> error_reason(100);
      SNPrintF(error_reason, "Internal wasm failure: %s", thrower.error_msg());
      ReportInstantiationFailure(script, position, error_reason.begin());
    } else {
      ReportInstantiationFailure(script, position, "Internal wasm failure");
    }
    thrower.Reset();  // Ensure exceptions do not propagate.
    return {};
  }
  DCHECK(!thrower.error());
  Handle<WasmInstanceObject> instance = maybe_instance.ToHandleChecked();

  ReportInstantiationSuccess(script, position,
                             instantiate_timer.Elapsed().InMillisecondsF());

  Handle<Name> single_function_name(
      isolate->factory()->InternalizeUtf8String(AsmJs::kSingleFunctionName));
  MaybeHandle<Object> single_function =
      Object::GetProperty(isolate, instance, single_function_name);
  if (!single_function.is_null() &&
      !IsUndefined(*single_function.ToHandleChecked(), isolate)) {
    return single_function;
  }

  // Here we rely on the fact that the exports object is eagerly created.
  // The following check is a weak indicator for that. If this ever changes,
  // then we'll have to call the "exports" getter, and be careful about
  // handling possible stack overflow exceptions.
  DCHECK(IsJSObject(instance->exports_object()));
  return handle(instance->exports_object(), isolate);
}

}  // namespace internal
}  // namespace v8

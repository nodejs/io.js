// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>

#include "include/v8-wasm.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/builtins/builtins-inl.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/frames-inl.h"
#include "src/heap/heap-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/smi.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/memory-tracing.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-serialization.h"

namespace v8::internal {

namespace {
V8_WARN_UNUSED_RESULT Tagged<Object> CrashUnlessFuzzing(Isolate* isolate) {
  CHECK(v8_flags.fuzzing);
  return ReadOnlyRoots(isolate).undefined_value();
}

struct WasmCompileControls {
  uint32_t MaxWasmBufferSize = std::numeric_limits<uint32_t>::max();
  bool AllowAnySizeForAsync = true;
};
using WasmCompileControlsMap = std::map<v8::Isolate*, WasmCompileControls>;

// We need per-isolate controls, because we sometimes run tests in multiple
// isolates concurrently. Methods need to hold the accompanying mutex on access.
// To avoid upsetting the static initializer count, we lazy initialize this.
DEFINE_LAZY_LEAKY_OBJECT_GETTER(WasmCompileControlsMap,
                                GetPerIsolateWasmControls)
base::LazyMutex g_PerIsolateWasmControlsMutex = LAZY_MUTEX_INITIALIZER;

bool IsWasmCompileAllowed(v8::Isolate* isolate, v8::Local<v8::Value> value,
                          bool is_async) {
  base::MutexGuard guard(g_PerIsolateWasmControlsMutex.Pointer());
  DCHECK_GT(GetPerIsolateWasmControls()->count(isolate), 0);
  const WasmCompileControls& ctrls = GetPerIsolateWasmControls()->at(isolate);
  return (is_async && ctrls.AllowAnySizeForAsync) ||
         (value->IsArrayBuffer() && value.As<v8::ArrayBuffer>()->ByteLength() <=
                                        ctrls.MaxWasmBufferSize) ||
         (value->IsArrayBufferView() &&
          value.As<v8::ArrayBufferView>()->ByteLength() <=
              ctrls.MaxWasmBufferSize);
}

// Use the compile controls for instantiation, too
bool IsWasmInstantiateAllowed(v8::Isolate* isolate,
                              v8::Local<v8::Value> module_or_bytes,
                              bool is_async) {
  base::MutexGuard guard(g_PerIsolateWasmControlsMutex.Pointer());
  DCHECK_GT(GetPerIsolateWasmControls()->count(isolate), 0);
  const WasmCompileControls& ctrls = GetPerIsolateWasmControls()->at(isolate);
  if (is_async && ctrls.AllowAnySizeForAsync) return true;
  if (!module_or_bytes->IsWasmModuleObject()) {
    return IsWasmCompileAllowed(isolate, module_or_bytes, is_async);
  }
  v8::Local<v8::WasmModuleObject> module =
      v8::Local<v8::WasmModuleObject>::Cast(module_or_bytes);
  return static_cast<uint32_t>(
             module->GetCompiledModule().GetWireBytesRef().size()) <=
         ctrls.MaxWasmBufferSize;
}

v8::Local<v8::Value> NewRangeException(v8::Isolate* isolate,
                                       const char* message) {
  return v8::Exception::RangeError(
      v8::String::NewFromOneByte(isolate,
                                 reinterpret_cast<const uint8_t*>(message))
          .ToLocalChecked());
}

void ThrowRangeException(v8::Isolate* isolate, const char* message) {
  isolate->ThrowException(NewRangeException(isolate, message));
}

bool WasmModuleOverride(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (IsWasmCompileAllowed(info.GetIsolate(), info[0], false)) return false;
  ThrowRangeException(info.GetIsolate(), "Sync compile not allowed");
  return true;
}

bool WasmInstanceOverride(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (IsWasmInstantiateAllowed(info.GetIsolate(), info[0], false)) return false;
  ThrowRangeException(info.GetIsolate(), "Sync instantiate not allowed");
  return true;
}

}  // namespace

// Returns a callable object. The object returns the difference of its two
// parameters when it is called.
RUNTIME_FUNCTION(Runtime_SetWasmCompileControls) {
  HandleScope scope(isolate);
  if (args.length() != 2 || !IsSmi(args[0]) || !IsBoolean(args[1])) {
    return CrashUnlessFuzzing(isolate);
  }
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  int block_size = args.smi_value_at(0);
  bool allow_async = Cast<Boolean>(args[1])->ToBool(isolate);
  base::MutexGuard guard(g_PerIsolateWasmControlsMutex.Pointer());
  WasmCompileControls& ctrl = (*GetPerIsolateWasmControls())[v8_isolate];
  ctrl.AllowAnySizeForAsync = allow_async;
  ctrl.MaxWasmBufferSize = static_cast<uint32_t>(block_size);
  v8_isolate->SetWasmModuleCallback(WasmModuleOverride);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_SetWasmInstantiateControls) {
  HandleScope scope(isolate);
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8_isolate->SetWasmInstanceCallback(WasmInstanceOverride);
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {

void PrintIndentation(int stack_size) {
  const int max_display = 80;
  if (stack_size <= max_display) {
    PrintF("%4d:%*s", stack_size, stack_size, "");
  } else {
    PrintF("%4d:%*s", stack_size, max_display, "...");
  }
}

int WasmStackSize(Isolate* isolate) {
  // TODO(wasm): Fix this for mixed JS/Wasm stacks with both --trace and
  // --trace-wasm.
  int n = 0;
  for (DebuggableStackFrameIterator it(isolate); !it.done(); it.Advance()) {
    if (it.is_wasm()) n++;
  }
  return n;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_CountUnoptimizedWasmToJSWrapper) {
  SealHandleScope shs(isolate);
  if (args.length() != 1 || !IsWasmInstanceObject(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Tagged<WasmInstanceObject> instance_object =
      Cast<WasmInstanceObject>(args[0]);
  Tagged<WasmTrustedInstanceData> trusted_data =
      instance_object->trusted_data(isolate);
  Address wrapper_start = isolate->builtins()
                              ->code(Builtin::kWasmToJsWrapperAsm)
                              ->instruction_start();
  int result = 0;
  Tagged<WasmDispatchTable> dispatch_table =
      trusted_data->dispatch_table_for_imports();
  int import_count = dispatch_table->length();
  for (int i = 0; i < import_count; ++i) {
    if (dispatch_table->target(i) == wrapper_start) {
      ++result;
    }
  }
  Tagged<ProtectedFixedArray> dispatch_tables = trusted_data->dispatch_tables();
  int table_count = dispatch_tables->length();
  for (int table_index = 0; table_index < table_count; ++table_index) {
    if (dispatch_tables->get(table_index) == Smi::zero()) continue;
    Tagged<WasmDispatchTable> table =
        Cast<WasmDispatchTable>(dispatch_tables->get(table_index));
    int table_size = table->length();
    for (int entry_index = 0; entry_index < table_size; ++entry_index) {
      if (table->target(entry_index) == wrapper_start) ++result;
    }
  }
  return Smi::FromInt(result);
}

RUNTIME_FUNCTION(Runtime_HasUnoptimizedWasmToJSWrapper) {
  SealHandleScope shs{isolate};
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Tagged<JSFunction> function = Cast<JSFunction>(args[0]);
  Tagged<SharedFunctionInfo> sfi = function->shared();
  if (!sfi->HasWasmFunctionData()) return isolate->heap()->ToBoolean(false);
  Tagged<WasmFunctionData> func_data = sfi->wasm_function_data();
  Address call_target = func_data->internal()->call_target();

  Address wrapper = Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate);
  return isolate->heap()->ToBoolean(call_target == wrapper);
}

RUNTIME_FUNCTION(Runtime_HasUnoptimizedJSToJSWrapper) {
  HandleScope shs(isolate);
  if (args.length() != 1) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<Object> param = args.at<Object>(0);
  if (!WasmJSFunction::IsWasmJSFunction(*param)) {
    return isolate->heap()->ToBoolean(false);
  }
  auto wasm_js_function = Cast<WasmJSFunction>(param);
  DirectHandle<WasmJSFunctionData> function_data(
      wasm_js_function->shared()->wasm_js_function_data(), isolate);

  DirectHandle<JSFunction> external_function =
      WasmInternalFunction::GetOrCreateExternal(
          handle(function_data->internal(), isolate));
  DirectHandle<Code> external_function_code(external_function->code(isolate),
                                            isolate);
  DirectHandle<Code> function_data_code(function_data->wrapper_code(isolate),
                                        isolate);
  Tagged<Code> wrapper = isolate->builtins()->code(Builtin::kJSToJSWrapper);
  // TODO(saelo): we have to use full pointer comparison here until all Code
  // objects are located in trusted space. Currently, builtin Code objects are
  // still inside the main pointer compression cage.
  static_assert(!kAllCodeObjectsLiveInTrustedSpace);
  if (!wrapper.SafeEquals(*external_function_code)) {
    return isolate->heap()->ToBoolean(false);
  }
  if (wrapper != *function_data_code) {
    return isolate->heap()->ToBoolean(false);
  }

  return isolate->heap()->ToBoolean(true);
}

RUNTIME_FUNCTION(Runtime_WasmTraceEnter) {
  HandleScope shs(isolate);
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  DCHECK_EQ(0, args.length());
  PrintIndentation(WasmStackSize(isolate));

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());

  // Find the function name.
  int func_index = frame->function_index();
  const wasm::WasmModule* module = frame->trusted_instance_data()->module();
  wasm::ModuleWireBytes wire_bytes =
      wasm::ModuleWireBytes(frame->native_module()->wire_bytes());
  wasm::WireBytesRef name_ref =
      module->lazily_generated_names.LookupFunctionName(wire_bytes, func_index);
  wasm::WasmName name = wire_bytes.GetNameOrNull(name_ref);

  wasm::WasmCode* code = frame->wasm_code();
  PrintF(code->is_liftoff() ? "~" : "*");

  if (name.empty()) {
    PrintF("wasm-function[%d] {\n", func_index);
  } else {
    PrintF("wasm-function[%d] \"%.*s\" {\n", func_index, name.length(),
           name.begin());
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceExit) {
  HandleScope shs(isolate);
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  DCHECK_EQ(1, args.length());
  Tagged<Smi> return_addr_smi = Cast<Smi>(args[0]);

  PrintIndentation(WasmStackSize(isolate));
  PrintF("}");

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());
  int func_index = frame->function_index();
  const wasm::WasmModule* module = frame->trusted_instance_data()->module();
  const wasm::FunctionSig* sig = module->functions[func_index].sig;

  size_t num_returns = sig->return_count();
  // If we have no returns, we should have passed {Smi::zero()}.
  DCHECK_IMPLIES(num_returns == 0, IsZero(return_addr_smi));
  if (num_returns == 1) {
    wasm::ValueType return_type = sig->GetReturn(0);
    switch (return_type.kind()) {
      case wasm::kI32: {
        int32_t value =
            base::ReadUnalignedValue<int32_t>(return_addr_smi.ptr());
        PrintF(" -> %d\n", value);
        break;
      }
      case wasm::kI64: {
        int64_t value =
            base::ReadUnalignedValue<int64_t>(return_addr_smi.ptr());
        PrintF(" -> %" PRId64 "\n", value);
        break;
      }
      case wasm::kF32: {
        float value = base::ReadUnalignedValue<float>(return_addr_smi.ptr());
        PrintF(" -> %f\n", value);
        break;
      }
      case wasm::kF64: {
        double value = base::ReadUnalignedValue<double>(return_addr_smi.ptr());
        PrintF(" -> %f\n", value);
        break;
      }
      default:
        PrintF(" -> Unsupported type\n");
        break;
    }
  } else {
    // TODO(wasm) Handle multiple return values.
    PrintF("\n");
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsAsmWasmCode) {
  SealHandleScope shs(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  auto function = Cast<JSFunction>(args[0]);
  if (!function->shared()->HasAsmWasmData()) {
    return ReadOnlyRoots(isolate).false_value();
  }
  if (function->shared()->HasBuiltinId() &&
      function->shared()->builtin_id() == Builtin::kInstantiateAsmJs) {
    // Hasn't been compiled yet.
    return ReadOnlyRoots(isolate).false_value();
  }
  return ReadOnlyRoots(isolate).true_value();
}

namespace {

bool DisallowWasmCodegenFromStringsCallback(v8::Local<v8::Context> context,
                                            v8::Local<v8::String> source) {
  return false;
}

}  // namespace

RUNTIME_FUNCTION(Runtime_DisallowWasmCodegen) {
  SealHandleScope shs(isolate);
  if (args.length() != 1 || !IsBoolean(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  bool flag = Cast<Boolean>(args[0])->ToBool(isolate);
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8_isolate->SetAllowWasmCodeGenerationCallback(
      flag ? DisallowWasmCodegenFromStringsCallback : nullptr);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsWasmCode) {
  SealHandleScope shs(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  auto function = Cast<JSFunction>(args[0]);
  Tagged<Code> code = function->code(isolate);
  bool is_js_to_wasm = code->kind() == CodeKind::JS_TO_WASM_FUNCTION ||
                       (code->builtin_id() == Builtin::kJSToWasmWrapper);
#if V8_ENABLE_DRUMBRAKE
  // TODO(paolosev@microsoft.com) - Implement an empty
  // GenericJSToWasmInterpreterWrapper also when V8_ENABLE_DRUMBRAKE is not
  // defined to get rid of these #ifdefs.
  is_js_to_wasm =
      is_js_to_wasm ||
      (code->builtin_id() == Builtin::kGenericJSToWasmInterpreterWrapper);
#endif  // V8_ENABLE_DRUMBRAKE
  return isolate->heap()->ToBoolean(is_js_to_wasm);
}

RUNTIME_FUNCTION(Runtime_IsWasmTrapHandlerEnabled) {
  DisallowGarbageCollection no_gc;
  return isolate->heap()->ToBoolean(trap_handler::IsTrapHandlerEnabled());
}

RUNTIME_FUNCTION(Runtime_IsWasmPartialOOBWriteNoop) {
  DisallowGarbageCollection no_gc;
  return isolate->heap()->ToBoolean(wasm::kPartialOOBWritesAreNoops);
}

RUNTIME_FUNCTION(Runtime_IsThreadInWasm) {
  DisallowGarbageCollection no_gc;
  return isolate->heap()->ToBoolean(trap_handler::IsThreadInWasm());
}

RUNTIME_FUNCTION(Runtime_GetWasmRecoveredTrapCount) {
  HandleScope scope(isolate);
  size_t trap_count = trap_handler::GetRecoveredTrapCount();
  return *isolate->factory()->NewNumberFromSize(trap_count);
}

RUNTIME_FUNCTION(Runtime_GetWasmExceptionTagId) {
  HandleScope scope(isolate);
  if (args.length() != 2 || !IsWasmExceptionPackage(args[0]) ||
      !IsWasmInstanceObject(args[1])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<WasmExceptionPackage> exception = args.at<WasmExceptionPackage>(0);
  DirectHandle<WasmInstanceObject> instance_object =
      args.at<WasmInstanceObject>(1);
  DirectHandle<WasmTrustedInstanceData> trusted_data(
      instance_object->trusted_data(isolate), isolate);
  DirectHandle<Object> tag =
      WasmExceptionPackage::GetExceptionTag(isolate, exception);
  CHECK(IsWasmExceptionTag(*tag));
  DirectHandle<FixedArray> tags_table(trusted_data->tags_table(), isolate);
  for (int index = 0; index < tags_table->length(); ++index) {
    if (tags_table->get(index) == *tag) return Smi::FromInt(index);
  }
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_GetWasmExceptionValues) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsWasmExceptionPackage(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<WasmExceptionPackage> exception = args.at<WasmExceptionPackage>(0);
  Handle<Object> values_obj =
      WasmExceptionPackage::GetExceptionValues(isolate, exception);
  if (!IsFixedArray(*values_obj)) {
    // Only called with correct input (unless fuzzing).
    return CrashUnlessFuzzing(isolate);
  }
  auto values = Cast<FixedArray>(values_obj);
  DirectHandle<FixedArray> externalized_values =
      isolate->factory()->NewFixedArray(values->length());
  for (int i = 0; i < values->length(); i++) {
    Handle<Object> value(values->get(i), isolate);
    if (!IsSmi(*value)) {
      // Note: This will leak string views to JS. This should be fine for a
      // debugging function.
      value = wasm::WasmToJSObject(isolate, value);
    }
    externalized_values->set(i, *value);
  }
  return *isolate->factory()->NewJSArrayWithElements(externalized_values);
}

RUNTIME_FUNCTION(Runtime_SerializeWasmModule) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsWasmModuleObject(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmModuleObject> module_obj = args.at<WasmModuleObject>(0);

  wasm::NativeModule* native_module = module_obj->native_module();
  DCHECK(!native_module->compilation_state()->failed());

  wasm::WasmSerializer wasm_serializer(native_module);
  size_t byte_length = wasm_serializer.GetSerializedNativeModuleSize();

  DirectHandle<JSArrayBuffer> array_buffer =
      isolate->factory()
          ->NewJSArrayBufferAndBackingStore(byte_length,
                                            InitializedFlag::kUninitialized)
          .ToHandleChecked();

  bool serialized_successfully = wasm_serializer.SerializeNativeModule(
      {static_cast<uint8_t*>(array_buffer->backing_store()), byte_length});
  CHECK(serialized_successfully || v8_flags.fuzzing);
  return *array_buffer;
}

// Take an array buffer and attempt to reconstruct a compiled wasm module.
// Return undefined if unsuccessful.
RUNTIME_FUNCTION(Runtime_DeserializeWasmModule) {
  HandleScope scope(isolate);
  if (args.length() != 2 || !IsJSArrayBuffer(args[0]) ||
      !IsJSTypedArray(args[1])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<JSArrayBuffer> buffer = args.at<JSArrayBuffer>(0);
  DirectHandle<JSTypedArray> wire_bytes = args.at<JSTypedArray>(1);
  if (buffer->was_detached() || wire_bytes->WasDetached()) {
    return CrashUnlessFuzzing(isolate);
  }

  DirectHandle<JSArrayBuffer> wire_bytes_buffer = wire_bytes->GetBuffer();
  base::Vector<const uint8_t> wire_bytes_vec{
      reinterpret_cast<const uint8_t*>(wire_bytes_buffer->backing_store()) +
          wire_bytes->byte_offset(),
      wire_bytes->byte_length()};
  base::Vector<uint8_t> buffer_vec{
      reinterpret_cast<uint8_t*>(buffer->backing_store()),
      buffer->byte_length()};

  // Note that {wasm::DeserializeNativeModule} will allocate. We assume the
  // JSArrayBuffer backing store doesn't get relocated.
  wasm::CompileTimeImports compile_imports{};
  MaybeHandle<WasmModuleObject> maybe_module_object =
      wasm::DeserializeNativeModule(isolate, buffer_vec, wire_bytes_vec,
                                    compile_imports, {});
  Handle<WasmModuleObject> module_object;
  if (!maybe_module_object.ToHandle(&module_object)) {
    return ReadOnlyRoots(isolate).undefined_value();
  }
  return *module_object;
}

RUNTIME_FUNCTION(Runtime_WasmGetNumberOfInstances) {
  SealHandleScope shs(isolate);
  if (args.length() != 1 || !IsWasmModuleObject(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmModuleObject> module_obj = args.at<WasmModuleObject>(0);
  int instance_count = 0;
  Tagged<WeakArrayList> weak_instance_list =
      module_obj->script()->wasm_weak_instance_list();
  for (int i = 0; i < weak_instance_list->length(); ++i) {
    if (weak_instance_list->Get(i).IsWeak()) instance_count++;
  }
  return Smi::FromInt(instance_count);
}

RUNTIME_FUNCTION(Runtime_WasmNumCodeSpaces) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSObject(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<JSObject> argument = args.at<JSObject>(0);
  wasm::NativeModule* native_module;
  if (IsWasmInstanceObject(*argument)) {
    native_module = Cast<WasmInstanceObject>(*argument)
                        ->trusted_data(isolate)
                        ->native_module();
  } else if (IsWasmModuleObject(*argument)) {
    native_module = Cast<WasmModuleObject>(*argument)->native_module();
  } else {
    UNREACHABLE();
  }
  size_t num_spaces = native_module->GetNumberOfCodeSpacesForTesting();
  return *isolate->factory()->NewNumberFromSize(num_spaces);
}

RUNTIME_FUNCTION(Runtime_WasmTraceMemory) {
  SealHandleScope scope(isolate);
  if (args.length() != 1 || !IsSmi(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DisallowGarbageCollection no_gc;
  auto info_addr = Cast<Smi>(args[0]);

  wasm::MemoryTracingInfo* info =
      reinterpret_cast<wasm::MemoryTracingInfo*>(info_addr.ptr());

  // Find the caller wasm frame.
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  DebuggableStackFrameIterator it(isolate);
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());

  // TODO(14259): Fix for multi-memory.
  auto memory_object = frame->trusted_instance_data()->memory_object(0);
  uint8_t* mem_start = reinterpret_cast<uint8_t*>(
      memory_object->array_buffer()->backing_store());
  int func_index = frame->function_index();
  int pos = frame->position();
  wasm::ExecutionTier tier = frame->wasm_code()->is_liftoff()
                                 ? wasm::ExecutionTier::kLiftoff
                                 : wasm::ExecutionTier::kTurbofan;
  wasm::TraceMemoryOperation(tier, info, func_index, pos, mem_start);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTierUpFunction) {
  DCHECK(!v8_flags.wasm_jitless);

  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<JSFunction> function = args.at<JSFunction>(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*function)) {
    return CrashUnlessFuzzing(isolate);
  }
  auto exp_fun = Cast<WasmExportedFunction>(function);
  auto func_data = exp_fun->shared()->wasm_exported_function_data();
  Tagged<WasmTrustedInstanceData> trusted_data = func_data->instance_data();
  int func_index = func_data->function_index();
  wasm::TierUpNowForTesting(isolate, trusted_data, func_index);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmNull) {
  // This isn't exposed to fuzzers. (Wasm nulls may not appear in JS.)
  HandleScope scope(isolate);
  return ReadOnlyRoots(isolate).wasm_null();
}

static Tagged<Object> CreateWasmObject(
    Isolate* isolate, base::Vector<const uint8_t> module_bytes) {
  // Create and compile the wasm module.
  wasm::ErrorThrower thrower(isolate, "CreateWasmObject");
  wasm::ModuleWireBytes bytes(base::VectorOf(module_bytes));
  wasm::WasmEngine* engine = wasm::GetWasmEngine();
  MaybeHandle<WasmModuleObject> maybe_module_object =
      engine->SyncCompile(isolate, wasm::WasmEnabledFeatures(),
                          wasm::CompileTimeImports(), &thrower, bytes);
  CHECK(!thrower.error());
  Handle<WasmModuleObject> module_object;
  if (!maybe_module_object.ToHandle(&module_object)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  // Instantiate the module.
  MaybeHandle<WasmInstanceObject> maybe_instance = engine->SyncInstantiate(
      isolate, &thrower, module_object, Handle<JSReceiver>::null(),
      MaybeHandle<JSArrayBuffer>());
  CHECK(!thrower.error());
  Handle<WasmInstanceObject> instance;
  if (!maybe_instance.ToHandle(&instance)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  // Get the exports.
  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  MaybeHandle<Object> maybe_exports =
      JSObject::GetProperty(isolate, instance, exports);
  Handle<Object> exports_object;
  if (!maybe_exports.ToHandle(&exports_object)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  // Get function and call it.
  Handle<Name> main_name = isolate->factory()->NewStringFromAsciiChecked("f");
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, Cast<JSObject>(exports_object), main_name, &desc);
  CHECK(property_found.FromMaybe(false));
  CHECK(IsJSFunction(*desc.value()));
  Handle<JSFunction> function = Cast<JSFunction>(desc.value());
  MaybeHandle<Object> maybe_result = Execution::Call(
      isolate, function, isolate->factory()->undefined_value(), 0, nullptr);
  Handle<Object> result;
  if (!maybe_result.ToHandle(&result)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  return *result;
}

// Creates a new wasm struct with one i64 (value 0x7AADF00DBAADF00D).
RUNTIME_FUNCTION(Runtime_WasmStruct) {
  HandleScope scope(isolate);
  /* Recreate with:
    d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(kWasmI64, false)]);
    builder.addFunction("f", makeSig([], [kWasmAnyRef]))
      .addBody([
        ...wasmI64Const(0x7AADF00DBAADF00Dn),
        kGCPrefix, kExprStructNew, struct
      ])
      .exportFunc();
    builder.instantiate();
  */
  static constexpr uint8_t wasm_module_bytes[] = {
      0x00, 0x61, 0x73, 0x6d,  // wasm magic
      0x01, 0x00, 0x00, 0x00,  // wasm version
      0x01,                    // section kind: Type
      0x0b,                    // section length 11
      0x02,                    // types count 2
      0x50, 0x00,              //  subtype extensible, supertype count 0
      0x5f, 0x01, 0x7e, 0x00,  //  kind: struct, field count 1:  i64 immutable
      0x60,                    //  kind: func
      0x00,                    // param count 0
      0x01, 0x6e,              // return count 1:  anyref
      0x03,                    // section kind: Function
      0x02,                    // section length 2
      0x01, 0x01,              // functions count 1: 0 $f (result anyref)
      0x07,                    // section kind: Export
      0x05,                    // section length 5
      0x01,                    // exports count 1: export # 0
      0x01,                    // field name length:  1
      0x66,                    // field name: f
      0x00, 0x00,              // kind: function index:  0
      0x0a,                    // section kind: Code
      0x12,                    // section length 18
      0x01,                    // functions count 1
                               // function #0 $f
      0x10,                    // body size 16
      0x00,                    // 0 entries in locals list
      0x42, 0x8d, 0xe0, 0xb7, 0xd5, 0xdb, 0x81, 0xfc, 0xd6, 0xfa,
      0x00,              // i64.const 8839985585355354125
      0xfb, 0x00, 0x00,  // struct.new $type0
      0x0b,              // end
  };
  return CreateWasmObject(isolate, base::VectorOf(wasm_module_bytes));
}

// Creates a new wasm array of type i32 with two elements (2x 0xBAADF00D).
RUNTIME_FUNCTION(Runtime_WasmArray) {
  HandleScope scope(isolate);
  /* Recreate with:
    d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
    let builder = new WasmModuleBuilder();
    let array = builder.addArray(kWasmI32, false);
    builder.addFunction("f", makeSig([], [kWasmAnyRef]))
      .addBody([
        ...wasmI32Const(0xBAADF00D), kExprI32Const, 2,
        kGCPrefix, kExprArrayNew, array
      ])
      .exportFunc();
  */
  static constexpr uint8_t wasm_module_bytes[] = {
      0x00, 0x61, 0x73, 0x6d,  // wasm magic
      0x01, 0x00, 0x00, 0x00,  // wasm version
      0x01,                    // section kind: Type
      0x0a,                    // section length 10
      0x02,                    // types count 2
      0x50, 0x00,              //  subtype extensible, supertype count 0
      0x5e, 0x7f, 0x00,        //  kind: array i32 immutable
      0x60,                    //  kind: func
      0x00,                    // param count 0
      0x01, 0x6e,              // return count 1:  anyref
      0x03,                    // section kind: Function
      0x02,                    // section length 2
      0x01, 0x01,              // functions count 1: 0 $f (result anyref)
      0x07,                    // section kind: Export
      0x05,                    // section length 5
      0x01,                    // exports count 1: export # 0
      0x01,                    // field name length:  1
      0x66,                    // field name: f
      0x00, 0x00,              // kind: function index:  0
      0x0a,                    // section kind: Code
      0x0f,                    // section length 15
      0x01,                    // functions count 1
                               // function #0 $f
      0x0d,                    // body size 13
      0x00,                    // 0 entries in locals list
      0x41, 0x8d, 0xe0, 0xb7, 0xd5, 0x7b,  // i32.const -1163005939
      0x41, 0x02,                          // i32.const 2
      0xfb, 0x06, 0x00,                    // array.new $type0
      0x0b,                                // end
  };
  return CreateWasmObject(isolate, base::VectorOf(wasm_module_bytes));
}

RUNTIME_FUNCTION(Runtime_WasmEnterDebugging) {
  HandleScope scope(isolate);
  wasm::GetWasmEngine()->EnterDebuggingForIsolate(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmLeaveDebugging) {
  HandleScope scope(isolate);
  wasm::GetWasmEngine()->LeaveDebuggingForIsolate(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsWasmDebugFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<JSFunction> function = args.at<JSFunction>(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*function)) {
    return CrashUnlessFuzzing(isolate);
  }
  auto exp_fun = Cast<WasmExportedFunction>(function);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_liftoff() &&
                                    code->for_debugging());
}

RUNTIME_FUNCTION(Runtime_IsLiftoffFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<JSFunction> function = args.at<JSFunction>(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*function)) {
    return CrashUnlessFuzzing(isolate);
  }
  auto exp_fun = Cast<WasmExportedFunction>(function);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_liftoff());
}

RUNTIME_FUNCTION(Runtime_IsTurboFanFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<JSFunction> function = args.at<JSFunction>(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*function)) {
    return CrashUnlessFuzzing(isolate);
  }
  auto exp_fun = Cast<WasmExportedFunction>(function);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_turbofan());
}

RUNTIME_FUNCTION(Runtime_IsUncompiledWasmFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsJSFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<JSFunction> function = args.at<JSFunction>(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*function)) {
    return CrashUnlessFuzzing(isolate);
  }
  auto exp_fun = Cast<WasmExportedFunction>(function);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  return isolate->heap()->ToBoolean(!native_module->HasCode(func_index));
}

RUNTIME_FUNCTION(Runtime_FreezeWasmLazyCompilation) {
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  DCHECK_EQ(args.length(), 1);
  DCHECK(IsWasmInstanceObject(args[0]));
  DisallowGarbageCollection no_gc;
  auto instance_object = Cast<WasmInstanceObject>(args[0]);

  instance_object->module_object()->native_module()->set_lazy_compile_frozen(
      true);
  return ReadOnlyRoots(isolate).undefined_value();
}

// This runtime function enables WebAssembly imported strings through an
// embedder callback and thereby bypasses the value in v8_flags.
RUNTIME_FUNCTION(Runtime_SetWasmImportedStringsEnabled) {
  if (args.length() != 1) {
    return CrashUnlessFuzzing(isolate);
  }
  bool enable = Object::BooleanValue(*args.at(0), isolate);
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  WasmImportedStringsEnabledCallback enabled = [](v8::Local<v8::Context>) {
    return true;
  };
  WasmImportedStringsEnabledCallback disabled = [](v8::Local<v8::Context>) {
    return false;
  };
  v8_isolate->SetWasmImportedStringsEnabledCallback(enable ? enabled
                                                           : disabled);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_FlushLiftoffCode) {
  auto [code_size, metadata_size] = wasm::GetWasmEngine()->FlushLiftoffCode();
  return Smi::FromInt(static_cast<int>(code_size + metadata_size));
}

RUNTIME_FUNCTION(Runtime_EstimateCurrentMemoryConsumption) {
  size_t result = wasm::GetWasmEngine()->EstimateCurrentMemoryConsumption();
  return Smi::FromInt(static_cast<int>(result));
}

RUNTIME_FUNCTION(Runtime_WasmCompiledExportWrappersCount) {
  int count = isolate->counters()
                  ->wasm_compiled_export_wrapper()
                  ->GetInternalPointer()
                  ->load();
  return Smi::FromInt(count);
}

RUNTIME_FUNCTION(Runtime_WasmDeoptsExecutedCount) {
  int count = wasm::GetWasmEngine()->GetDeoptsExecutedCount();
  return Smi::FromInt(count);
}

RUNTIME_FUNCTION(Runtime_WasmDeoptsExecutedForFunction) {
  if (args.length() != 1) {
    return CrashUnlessFuzzing(isolate);
  }
  Handle<Object> arg = args.at(0);
  if (!WasmExportedFunction::IsWasmExportedFunction(*arg)) {
    return Smi::FromInt(-1);
  }
  auto wasm_func = Cast<WasmExportedFunction>(arg);
  auto func_data = wasm_func->shared()->wasm_exported_function_data();
  const wasm::WasmModule* module =
      func_data->instance_data()->native_module()->module();
  uint32_t func_index = func_data->function_index();
  const wasm::TypeFeedbackStorage& feedback = module->type_feedback;
  base::SharedMutexGuard<base::kExclusive> mutex_guard(&feedback.mutex);
  auto entry = feedback.deopt_count_for_function.find(func_index);
  if (entry == feedback.deopt_count_for_function.end()) {
    return Smi::FromInt(0);
  }
  return Smi::FromInt(entry->second);
}

RUNTIME_FUNCTION(Runtime_WasmSwitchToTheCentralStackCount) {
  int count = isolate->wasm_switch_to_the_central_stack_counter();
  return Smi::FromInt(count);
}

RUNTIME_FUNCTION(Runtime_CheckIsOnCentralStack) {
  // This function verifies that itself, and therefore the JS function that
  // called it, is running on the central stack. This is used to check that wasm
  // switches to the central stack to run JS imports.
  CHECK(isolate->IsOnCentralStack());
  return ReadOnlyRoots(isolate).undefined_value();
}

// The GenerateRandomWasmModule function is only implemented in non-official
// builds (to save binary size). Hence also skip the runtime function in
// official builds.
#ifndef OFFICIAL_BUILD
RUNTIME_FUNCTION(Runtime_WasmGenerateRandomModule) {
  HandleScope scope{isolate};
  Zone temporary_zone{isolate->allocator(), "WasmGenerateRandomModule"};
  constexpr size_t kMaxInputBytes = 512;
  ZoneVector<uint8_t> input_bytes{&temporary_zone};
  auto add_input_bytes = [&input_bytes](void* bytes, size_t max_bytes) {
    size_t num_bytes = std::min(kMaxInputBytes - input_bytes.size(), max_bytes);
    input_bytes.resize(input_bytes.size() + num_bytes);
    memcpy(input_bytes.end() - num_bytes, bytes, num_bytes);
  };
  if (args.length() == 0) {
    // If we are called without any arguments, use the RNG from the isolate to
    // generate between 1 and kMaxInputBytes random bytes.
    int num_bytes =
        1 + isolate->random_number_generator()->NextInt(kMaxInputBytes);
    input_bytes.resize(num_bytes);
    isolate->random_number_generator()->NextBytes(input_bytes.data(),
                                                  num_bytes);
  } else {
    for (int i = 0; i < args.length(); ++i) {
      if (IsJSTypedArray(args[i])) {
        Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(args[i]);
        add_input_bytes(typed_array->DataPtr(), typed_array->GetByteLength());
      } else if (IsJSArrayBuffer(args[i])) {
        Tagged<JSArrayBuffer> array_buffer = Cast<JSArrayBuffer>(args[i]);
        add_input_bytes(array_buffer->backing_store(),
                        array_buffer->GetByteLength());
      } else if (IsSmi(args[i])) {
        int smi_value = Cast<Smi>(args[i]).value();
        add_input_bytes(&smi_value, kIntSize);
      } else if (IsHeapNumber(args[i])) {
        double value = Cast<HeapNumber>(args[i])->value();
        add_input_bytes(&value, kDoubleSize);
      } else {
        // TODO(14637): Extract bytes from more types.
      }
    }
  }

  // Don't limit any expressions in the generated Wasm module.
  constexpr auto options =
      wasm::fuzzing::WasmModuleGenerationOptions::kGenerateAll;
  base::Vector<const uint8_t> module_bytes =
      wasm::fuzzing::GenerateRandomWasmModule<options>(
          &temporary_zone, base::VectorOf(input_bytes));

  if (module_bytes.empty()) return ReadOnlyRoots(isolate).undefined_value();

  wasm::ErrorThrower thrower{isolate, "WasmGenerateRandomModule"};
  MaybeHandle<WasmModuleObject> maybe_module_object =
      wasm::GetWasmEngine()->SyncCompile(isolate,
                                         wasm::WasmEnabledFeatures::FromFlags(),
                                         wasm::CompileTimeImports{}, &thrower,
                                         wasm::ModuleWireBytes{module_bytes});
  if (thrower.error()) {
    FATAL(
        "wasm::GenerateRandomWasmModule produced a module which did not "
        "compile: %s",
        thrower.error_msg());
  }
  return *maybe_module_object.ToHandleChecked();
}
#endif  // OFFICIAL_BUILD

}  // namespace v8::internal

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <type_traits>

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
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/memory-tracing.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-code-pointer-table-inl.h"
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
  Address wrapper_entry =
      Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate);

  int result = 0;
  Tagged<WasmDispatchTable> dispatch_table =
      trusted_data->dispatch_table_for_imports();
  int import_count = dispatch_table->length();
  wasm::WasmCodePointerTable* cpt = wasm::GetProcessWideWasmCodePointerTable();
  for (int i = 0; i < import_count; ++i) {
    if (cpt->EntrypointEqualTo(dispatch_table->target(i), wrapper_entry)) {
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
      WasmCodePointer target = table->target(entry_index);
      if (target != wasm::kInvalidWasmCodePointer &&
          cpt->EntrypointEqualTo(target, wrapper_entry))
        ++result;
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
  WasmCodePointer call_target = func_data->internal()->call_target();

  Address wrapper_entry =
      Builtins::EntryOf(Builtin::kWasmToJsWrapperAsm, isolate);
  return isolate->heap()->ToBoolean(
      wasm::GetProcessWideWasmCodePointerTable()->EntrypointEqualTo(
          call_target, wrapper_entry));
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
          direct_handle(function_data->internal(), isolate));
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
      !IsWasmInstanceObject(args[1]) ||
      !args.at<WasmInstanceObject>(1)
           ->trusted_data(isolate)
           ->has_tags_table()) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExceptionPackage> exception =
      args.at<WasmExceptionPackage>(0);
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
  return CrashUnlessFuzzing(isolate);
}

RUNTIME_FUNCTION(Runtime_GetWasmExceptionValues) {
  HandleScope scope(isolate);
  if (args.length() != 1 || !IsWasmExceptionPackage(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExceptionPackage> exception =
      args.at<WasmExceptionPackage>(0);
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
  // This isn't exposed to fuzzers so doesn't need to handle invalid arguments.
  CHECK_EQ(2, args.length());
  CHECK(IsJSArrayBuffer(args[0]));
  CHECK(IsJSTypedArray(args[1]));

  DirectHandle<JSArrayBuffer> buffer = args.at<JSArrayBuffer>(0);
  DirectHandle<JSTypedArray> wire_bytes = args.at<JSTypedArray>(1);
  CHECK(!buffer->was_detached());
  CHECK(!wire_bytes->WasDetached());

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
  MaybeDirectHandle<WasmModuleObject> maybe_module_object =
      wasm::DeserializeNativeModule(isolate, buffer_vec, wire_bytes_vec,
                                    compile_imports, {});
  DirectHandle<WasmModuleObject> module_object;
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
    return CrashUnlessFuzzing(isolate);
  }
  size_t num_spaces = native_module->GetNumberOfCodeSpacesForTesting();
  return *isolate->factory()->NewNumberFromSize(num_spaces);
}

namespace {

template <typename T1, typename T2 = T1>
void PrintRep(Address address, const char* str) {
  PrintF("%4s:", str);
  const auto t1 = base::ReadLittleEndianValue<T1>(address);
  if constexpr (std::is_floating_point_v<T1>) {
    PrintF("%f", t1);
  } else if constexpr (sizeof(T1) > sizeof(uint32_t)) {
    PrintF("%" PRIu64, t1);
  } else {
    PrintF("%u", t1);
  }
  const auto t2 = base::ReadLittleEndianValue<T2>(address);
  if constexpr (sizeof(T1) > sizeof(uint32_t)) {
    PrintF(" / %016" PRIx64 "\n", t2);
  } else {
    PrintF(" / %0*x\n", static_cast<int>(2 * sizeof(T2)), t2);
  }
}

}  // namespace

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
  DebuggableStackFrameIterator it(isolate, StackFrameIterator::NoHandles{});
  DCHECK(!it.done());
  DCHECK(it.is_wasm());
#if V8_ENABLE_DRUMBRAKE
  DCHECK(!it.is_wasm_interpreter_entry());
#endif  // V8_ENABLE_DRUMBRAKE
  WasmFrame* frame = WasmFrame::cast(it.frame());

  PrintF("%-11s func:%6d:0x%-4x %s %016" PRIuPTR " val: ",
         ExecutionTierToString(frame->wasm_code()->is_liftoff()
                                   ? wasm::ExecutionTier::kLiftoff
                                   : wasm::ExecutionTier::kTurbofan),
         frame->function_index(), frame->position(),
         // Note: The extra leading space makes " store to" the same width as
         // "load from".
         info->is_store ? " store to" : "load from", info->offset);
  // TODO(14259): Fix for multi-memory.
  const Address address =
      reinterpret_cast<Address>(frame->trusted_instance_data()
                                    ->memory_object(0)
                                    ->array_buffer()
                                    ->backing_store()) +
      info->offset;
  switch (static_cast<MachineRepresentation>(info->mem_rep)) {
    case MachineRepresentation::kWord8:
      PrintRep<uint8_t>(address, "i8");
      break;
    case MachineRepresentation::kWord16:
      PrintRep<uint16_t>(address, "i16");
      break;
    case MachineRepresentation::kWord32:
      PrintRep<uint32_t>(address, "i32");
      break;
    case MachineRepresentation::kWord64:
      PrintRep<uint64_t>(address, "i64");
      break;
    case MachineRepresentation::kFloat32:
      PrintRep<float, uint32_t>(address, "f32");
      break;
    case MachineRepresentation::kFloat64:
      PrintRep<double, uint64_t>(address, "f64");
      break;
    case MachineRepresentation::kSimd128: {
      const auto a = base::ReadLittleEndianValue<uint32_t>(address);
      const auto b = base::ReadLittleEndianValue<uint32_t>(address + 4);
      const auto c = base::ReadLittleEndianValue<uint32_t>(address + 8);
      const auto d = base::ReadLittleEndianValue<uint32_t>(address + 12);
      PrintF("s128:%u %u %u %u / %08x %08x %08x %08x\n", a, b, c, d, a, b, c,
             d);
      break;
    }
    default:
      PrintF("unknown\n");
      break;
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {
// Validate a function now if not already validated. Returns false on validation
// failure, true otherwise.
V8_WARN_UNUSED_RESULT
bool ValidateFunctionNowIfNeeded(Isolate* isolate,
                                 wasm::NativeModule* native_module,
                                 int func_index) {
  const wasm::WasmModule* module = native_module->module();
  if (module->function_was_validated(func_index)) return true;
  DCHECK(v8_flags.wasm_lazy_validation);
  Zone validation_zone(isolate->allocator(), ZONE_NAME);
  wasm::WasmDetectedFeatures unused_detected_features;
  const wasm::WasmFunction* func = &module->functions[func_index];
  bool is_shared = module->type(func->sig_index).is_shared;
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  wasm::FunctionBody body{
      func->sig, func->code.offset(), wire_bytes.begin() + func->code.offset(),
      wire_bytes.begin() + func->code.end_offset(), is_shared};
  if (ValidateFunctionBody(&validation_zone, native_module->enabled_features(),
                           module, &unused_detected_features, body)
          .failed()) {
    return false;
  }
  module->set_function_validated(func_index);
  return true;
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmTierUpFunction) {
  DCHECK(!v8_flags.wasm_jitless);

  HandleScope scope(isolate);
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto func_data = exp_fun->shared()->wasm_exported_function_data();
  Tagged<WasmTrustedInstanceData> trusted_data = func_data->instance_data();
  int func_index = func_data->function_index();
  wasm::NativeModule* native_module = trusted_data->native_module();
  const wasm::WasmModule* module = native_module->module();
  if (static_cast<uint32_t>(func_index) < module->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  if (!ValidateFunctionNowIfNeeded(isolate, native_module, func_index)) {
    return CrashUnlessFuzzing(isolate);
  }
  wasm::TierUpNowForTesting(isolate, trusted_data, func_index);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTriggerTierUpForTesting) {
  DCHECK(!v8_flags.wasm_jitless);

  HandleScope scope(isolate);
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto func_data = exp_fun->shared()->wasm_exported_function_data();
  Tagged<WasmTrustedInstanceData> trusted_data = func_data->instance_data();
  int func_index = func_data->function_index();
  wasm::NativeModule* native_module = trusted_data->native_module();
  const wasm::WasmModule* module = native_module->module();
  if (static_cast<uint32_t>(func_index) < module->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  if (!ValidateFunctionNowIfNeeded(isolate, native_module, func_index)) {
    return CrashUnlessFuzzing(isolate);
  }
  wasm::TriggerTierUp(isolate, trusted_data, func_index);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmNull) {
  // This isn't exposed to fuzzers. (Wasm nulls may not appear in JS.)
  HandleScope scope(isolate);
  return ReadOnlyRoots(isolate).wasm_null();
}

static Tagged<Object> CreateWasmObject(Isolate* isolate,
                                       base::Vector<const uint8_t> module_bytes,
                                       bool is_struct) {
  if (module_bytes.size() > v8_flags.wasm_max_module_size) {
    return CrashUnlessFuzzing(isolate);
  }
  // Create and compile the wasm module.
  wasm::ErrorThrower thrower(isolate, "CreateWasmObject");
  base::OwnedVector<const uint8_t> bytes = base::OwnedCopyOf(module_bytes);
  wasm::WasmEngine* engine = wasm::GetWasmEngine();
  MaybeHandle<WasmModuleObject> maybe_module_object = engine->SyncCompile(
      isolate, wasm::WasmEnabledFeatures(), wasm::CompileTimeImports(),
      &thrower, std::move(bytes));
  CHECK(!thrower.error());
  Handle<WasmModuleObject> module_object;
  if (!maybe_module_object.ToHandle(&module_object)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  // Instantiate the module.
  MaybeDirectHandle<WasmInstanceObject> maybe_instance =
      engine->SyncInstantiate(isolate, &thrower, module_object,
                              Handle<JSReceiver>::null(),
                              MaybeHandle<JSArrayBuffer>());
  CHECK(!thrower.error());
  DirectHandle<WasmInstanceObject> instance;
  if (!maybe_instance.ToHandle(&instance)) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  wasm::WasmValue value(int64_t{0x7AADF00DBAADF00D});
  wasm::ModuleTypeIndex type_index{0};
  Tagged<Map> map = Tagged<Map>::cast(
      instance->trusted_data(isolate)->managed_object_maps()->get(
          type_index.index));
  if (is_struct) {
    const wasm::StructType* struct_type =
        instance->module()->struct_type(type_index);
    DCHECK_EQ(struct_type->field_count(), 1);
    DCHECK_EQ(struct_type->field(0), wasm::kWasmI64);
    return *isolate->factory()->NewWasmStruct(struct_type, &value,
                                              direct_handle(map, isolate));
  } else {
    DCHECK_EQ(instance->module()->array_type(type_index)->element_type(),
              wasm::kWasmI64);
    return *isolate->factory()->NewWasmArray(wasm::kWasmI64, 1, value,
                                             direct_handle(map, isolate));
  }
}

// In jitless mode we don't support creation of real wasm objects (they require
// a NativeModule and instantiating that is not supported in jitless), so this
// function creates a frozen JS object that should behave the same as a wasm
// object within JS.
static Tagged<Object> CreateDummyWasmLookAlikeForFuzzing(Isolate* isolate) {
  DirectHandle<JSObject> obj = isolate->factory()->NewJSObjectWithNullProto();
  CHECK(IsJSReceiver(*obj));
  MAYBE_RETURN(JSReceiver::SetIntegrityLevel(isolate, Cast<JSReceiver>(obj),
                                             FROZEN, kThrowOnError),
               ReadOnlyRoots(isolate).exception());
  return *obj;
}

// Creates a new wasm struct with one i64 (value 0x7AADF00DBAADF00D).
RUNTIME_FUNCTION(Runtime_WasmStruct) {
  HandleScope scope(isolate);
  if (v8_flags.jitless && !v8_flags.wasm_jitless) {
    return CreateDummyWasmLookAlikeForFuzzing(isolate);
  }
  /* Recreate with:
    d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(kWasmI64, false)]);
    builder.instantiate();
  */
  static constexpr uint8_t wasm_module_bytes[] = {
      0x00, 0x61, 0x73, 0x6d,  // wasm magic
      0x01, 0x00, 0x00, 0x00,  // wasm version
      0x01,                    // section kind: Type
      0x07,                    // section length 7
      0x01, 0x50, 0x00,        // types count 1:  subtype extensible,
                               // supertype count 0
      0x5f, 0x01, 0x7e, 0x00,  //  kind: struct, field count 1:  i64 immutable
  };
  return CreateWasmObject(isolate, base::VectorOf(wasm_module_bytes), true);
}

// Creates a new wasm array of type i64 with one element (0x7AADF00DBAADF00D).
RUNTIME_FUNCTION(Runtime_WasmArray) {
  HandleScope scope(isolate);
  if (v8_flags.jitless && !v8_flags.wasm_jitless) {
    return CreateDummyWasmLookAlikeForFuzzing(isolate);
  }
  /* Recreate with:
    d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
    let builder = new WasmModuleBuilder();
    let array = builder.addArray(kWasmI64, false);
    builder.instantiate();
  */
  static constexpr uint8_t wasm_module_bytes[] = {
      0x00, 0x61, 0x73, 0x6d,  // wasm magic
      0x01, 0x00, 0x00, 0x00,  // wasm version
      0x01,                    // section kind: Type
      0x06,                    // section length 6
      0x01, 0x50, 0x00,        // types count 1:  subtype extensible,
                               // supertype count 0
      0x5e, 0x7e, 0x00,        //  kind: array i64 immutable
  };
  return CreateWasmObject(isolate, base::VectorOf(wasm_module_bytes), false);
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
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  if (static_cast<uint32_t>(func_index) <
      data->instance_data()->module()->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_liftoff() &&
                                    code->for_debugging());
}

RUNTIME_FUNCTION(Runtime_IsLiftoffFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  if (static_cast<uint32_t>(func_index) <
      data->instance_data()->module()->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_liftoff());
}

RUNTIME_FUNCTION(Runtime_IsTurboFanFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  if (static_cast<uint32_t>(func_index) <
      data->instance_data()->module()->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  wasm::WasmCodeRefScope code_ref_scope;
  wasm::WasmCode* code = native_module->GetCode(func_index);
  return isolate->heap()->ToBoolean(code && code->is_turbofan());
}

RUNTIME_FUNCTION(Runtime_IsUncompiledWasmFunction) {
  HandleScope scope(isolate);
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto data = exp_fun->shared()->wasm_exported_function_data();
  wasm::NativeModule* native_module = data->instance_data()->native_module();
  uint32_t func_index = data->function_index();
  if (static_cast<uint32_t>(func_index) <
      data->instance_data()->module()->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
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
  if (args.length() != 1 ||
      !WasmExportedFunction::IsWasmExportedFunction(args[0])) {
    return CrashUnlessFuzzing(isolate);
  }
  DirectHandle<WasmExportedFunction> exp_fun = args.at<WasmExportedFunction>(0);
  auto func_data = exp_fun->shared()->wasm_exported_function_data();
  const wasm::WasmModule* module =
      func_data->instance_data()->native_module()->module();
  uint32_t func_index = func_data->function_index();
  if (static_cast<uint32_t>(func_index) <
      func_data->instance_data()->module()->num_imported_functions) {
    return CrashUnlessFuzzing(isolate);
  }
  const wasm::TypeFeedbackStorage& feedback = module->type_feedback;
  base::SpinningMutexGuard mutex_guard(&feedback.mutex);
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
#ifdef V8_WASM_RANDOM_FUZZERS
RUNTIME_FUNCTION(Runtime_WasmGenerateRandomModule) {
  if (v8_flags.jitless) {
    return CrashUnlessFuzzing(isolate);
  }
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

  // Avoid generating SIMD if the CPU does not support it, or it's disabled via
  // flags. Otherwise, do not limit the generated expressions and types.
  constexpr auto kAllOptions =
      wasm::fuzzing::WasmModuleGenerationOptions::All();
  constexpr auto kNoSimdOptions = wasm::fuzzing::WasmModuleGenerationOptions{
      {wasm::fuzzing::WasmModuleGenerationOption::kGenerateWasmGC}};
  static_assert(
      (kNoSimdOptions |
       wasm::fuzzing::WasmModuleGenerationOptions{
           {wasm::fuzzing::WasmModuleGenerationOption::kGenerateSIMD}}) ==
      kAllOptions);
  auto options =
      wasm::CheckHardwareSupportsSimd() ? kAllOptions : kNoSimdOptions;

  base::Vector<const uint8_t> module_bytes =
      wasm::fuzzing::GenerateRandomWasmModule(&temporary_zone, options,
                                              base::VectorOf(input_bytes));

  // Fuzzers can set `--wasm-max-module-size` to small values and then call
  // %WasmGenerateRandomModule() (see https://crbug.com/382816108).
  if (module_bytes.size() > v8_flags.wasm_max_module_size) {
    return CrashUnlessFuzzing(isolate);
  }

  wasm::ErrorThrower thrower{isolate, "WasmGenerateRandomModule"};
  MaybeDirectHandle<WasmModuleObject> maybe_module_object =
      wasm::GetWasmEngine()->SyncCompile(isolate,
                                         wasm::WasmEnabledFeatures::FromFlags(),
                                         wasm::CompileTimeImports{}, &thrower,
                                         base::OwnedCopyOf(module_bytes));
  if (thrower.error()) {
    FATAL(
        "wasm::GenerateRandomWasmModule produced a module which did not "
        "compile: %s",
        thrower.error_msg());
  }
  return *maybe_module_object.ToHandleChecked();
}
#endif  // V8_WASM_RANDOM_FUZZERS

}  // namespace v8::internal

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/common/wasm/wasm-module-runner.h"

#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace testing {

MaybeHandle<WasmModuleObject> CompileForTesting(Isolate* isolate,
                                                ErrorThrower* thrower,
                                                ModuleWireBytes bytes) {
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  MaybeHandle<WasmModuleObject> module = GetWasmEngine()->SyncCompile(
      isolate, enabled_features, CompileTimeImports{}, thrower, bytes);
  DCHECK_EQ(thrower->error(), module.is_null());
  return module;
}

MaybeHandle<WasmInstanceObject> CompileAndInstantiateForTesting(
    Isolate* isolate, ErrorThrower* thrower, ModuleWireBytes bytes) {
  MaybeHandle<WasmModuleObject> module =
      CompileForTesting(isolate, thrower, bytes);
  if (module.is_null()) return {};
  return GetWasmEngine()->SyncInstantiate(isolate, thrower,
                                          module.ToHandleChecked(), {}, {});
}

base::OwnedVector<Handle<Object>> MakeDefaultArguments(Isolate* isolate,
                                                       const FunctionSig* sig) {
  size_t param_count = sig->parameter_count();
  auto arguments = base::OwnedVector<Handle<Object>>::New(param_count);

  for (size_t i = 0; i < param_count; ++i) {
    switch (sig->GetParam(i).kind()) {
      case kI32:
      case kF32:
      case kF64:
      case kS128:
        // Argument here for kS128 does not matter as we should error out before
        // hitting this case.
        arguments[i] = handle(Smi::FromInt(static_cast<int>(i)), isolate);
        break;
      case kI64:
        arguments[i] = BigInt::FromInt64(isolate, static_cast<int64_t>(i));
        break;
      case kRefNull:
        arguments[i] = isolate->factory()->null_value();
        break;
      case kRef:
        arguments[i] = isolate->factory()->undefined_value();
        break;
      case kRtt:
      case kI8:
      case kI16:
      case kF16:
      case kVoid:
      case kBottom:
        UNREACHABLE();
    }
  }

  return arguments;
}

int32_t CompileAndRunWasmModule(Isolate* isolate, const uint8_t* module_start,
                                const uint8_t* module_end) {
  HandleScope scope(isolate);
  ErrorThrower thrower(isolate, "CompileAndRunWasmModule");
  MaybeHandle<WasmInstanceObject> instance = CompileAndInstantiateForTesting(
      isolate, &thrower, ModuleWireBytes(module_start, module_end));
  if (instance.is_null()) {
    return -1;
  }
  return CallWasmFunctionForTesting(isolate, instance.ToHandleChecked(), "main",
                                    {});
}

MaybeHandle<WasmExportedFunction> GetExportedFunction(
    Isolate* isolate, Handle<WasmInstanceObject> instance, const char* name) {
  Handle<JSObject> exports_object;
  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  exports_object = Cast<JSObject>(
      JSObject::GetProperty(isolate, instance, exports).ToHandleChecked());

  Handle<Name> main_name = isolate->factory()->NewStringFromAsciiChecked(name);
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  if (!property_found.FromMaybe(false)) return {};
  if (!IsJSFunction(*desc.value())) return {};

  return Cast<WasmExportedFunction>(desc.value());
}

int32_t CallWasmFunctionForTesting(Isolate* isolate,
                                   Handle<WasmInstanceObject> instance,
                                   const char* name,
                                   base::Vector<Handle<Object>> args,
                                   std::unique_ptr<const char[]>* exception) {
  DCHECK_IMPLIES(exception != nullptr, *exception == nullptr);
  MaybeHandle<WasmExportedFunction> maybe_export =
      GetExportedFunction(isolate, instance, name);
  Handle<WasmExportedFunction> exported_function;
  if (!maybe_export.ToHandle(&exported_function)) {
    return -1;
  }

  // Call the JS function.
  Handle<Object> undefined = isolate->factory()->undefined_value();
  MaybeHandle<Object> retval = Execution::Call(
      isolate, exported_function, undefined, args.length(), args.begin());

  // The result should be a number.
  if (retval.is_null()) {
    DCHECK(isolate->has_exception());
    if (exception) {
      DirectHandle<String> exception_string = Object::NoSideEffectsToString(
          isolate, direct_handle(isolate->exception(), isolate));
      *exception = exception_string->ToCString();
    }
    isolate->clear_internal_exception();
    return -1;
  }
  Handle<Object> result = retval.ToHandleChecked();

  // Multi-value returns, get the first return value (see InterpretWasmModule).
  if (IsJSArray(*result)) {
    auto receiver = Cast<JSReceiver>(result);
    result = JSObject::GetElement(isolate, receiver, 0).ToHandleChecked();
  }

  if (IsSmi(*result)) {
    return Smi::ToInt(*result);
  }
  if (IsHeapNumber(*result)) {
    return static_cast<int32_t>(Cast<HeapNumber>(*result)->value());
  }
  if (IsBigInt(*result)) {
    return static_cast<int32_t>(Cast<BigInt>(*result)->AsInt64());
  }
  return -1;
}

void SetupIsolateForWasmModule(Isolate* isolate) {
  WasmJs::Install(isolate, true);
}

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8

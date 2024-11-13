// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

// This fuzzer fuzzes initializer expressions used e.g. in globals.
// The fuzzer creates a set of globals with initializer expressions and a set of
// functions containing the same body as these initializer expressions.
// The global value should be equal to the result of running the corresponding
// function.

namespace v8::internal::wasm::fuzzing {

#define CHECK_FLOAT_EQ(expected, actual)                    \
  if (std::isnan(expected)) {                               \
    CHECK(std::isnan(actual));                              \
  } else {                                                  \
    CHECK_EQ(expected, actual);                             \
    CHECK_EQ(std::signbit(expected), std::signbit(actual)); \
  }

namespace {
bool IsNullOrWasmNull(Tagged<Object> obj) {
  return IsNull(obj) || IsWasmNull(obj);
}

Handle<Object> GetExport(Isolate* isolate, Handle<WasmInstanceObject> instance,
                         const char* name) {
  Handle<JSObject> exports_object;
  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  exports_object = Cast<JSObject>(
      JSObject::GetProperty(isolate, instance, exports).ToHandleChecked());

  Handle<Name> main_name = isolate->factory()->NewStringFromAsciiChecked(name);
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  CHECK(property_found.FromMaybe(false));
  return desc.value();
}

void CheckEquivalent(const WasmValue& lhs, const WasmValue& rhs,
                     const WasmModule& module) {
  DisallowGarbageCollection no_gc;
  // Stack of elements to be checked.
  std::vector<std::pair<WasmValue, WasmValue>> cmp = {{lhs, rhs}};
  using TaggedT = decltype(Tagged<Object>().ptr());
  // Map of lhs objects we have already seen to their rhs object on the first
  // visit. This is needed to ensure a reasonable runtime for the check.
  // Example:
  //   (array.new $myArray 10 (array.new_default $myArray 10))
  // This creates a nested array where each outer array element is the same
  // inner array. Without memorizing the inner array, we'd end up performing
  // 100+ comparisons.
  std::unordered_map<TaggedT, TaggedT> lhs_map;
  auto SeenAlready = [&lhs_map](Tagged<Object> lhs, Tagged<Object> rhs) {
    auto [iter, inserted] = lhs_map.insert({lhs.ptr(), rhs.ptr()});
    if (inserted) return false;
    CHECK_EQ(iter->second, rhs.ptr());
    return true;
  };

  auto CheckArray = [&cmp, &SeenAlready](Tagged<Object> lhs,
                                         Tagged<Object> rhs) {
    if (SeenAlready(lhs, rhs)) return;
    CHECK(IsWasmArray(lhs));
    CHECK(IsWasmArray(rhs));
    Tagged<WasmArray> lhs_array = Cast<WasmArray>(lhs);
    Tagged<WasmArray> rhs_array = Cast<WasmArray>(rhs);
    CHECK_EQ(lhs_array->map(), rhs_array->map());
    CHECK_EQ(lhs_array->length(), rhs_array->length());
    cmp.reserve(cmp.size() + lhs_array->length());
    for (uint32_t i = 0; i < lhs_array->length(); ++i) {
      cmp.emplace_back(lhs_array->GetElement(i), rhs_array->GetElement(i));
    }
  };

  auto CheckStruct = [&cmp, &SeenAlready](Tagged<Object> lhs,
                                          Tagged<Object> rhs) {
    if (SeenAlready(lhs, rhs)) return;
    CHECK(IsWasmStruct(lhs));
    CHECK(IsWasmStruct(rhs));
    Tagged<WasmStruct> lhs_struct = Cast<WasmStruct>(lhs);
    Tagged<WasmStruct> rhs_struct = Cast<WasmStruct>(rhs);
    CHECK_EQ(lhs_struct->map(), rhs_struct->map());
    uint32_t field_count = lhs_struct->type()->field_count();
    for (uint32_t i = 0; i < field_count; ++i) {
      cmp.emplace_back(lhs_struct->GetFieldValue(i),
                       rhs_struct->GetFieldValue(i));
    }
  };

  // Compare the function result with the global value.
  while (!cmp.empty()) {
    const auto [lhs, rhs] = cmp.back();
    cmp.pop_back();
    CHECK_EQ(lhs.type(), rhs.type());
    switch (lhs.type().kind()) {
      case ValueKind::kF32:
        CHECK_FLOAT_EQ(lhs.to_f32(), rhs.to_f32());
        break;
      case ValueKind::kF64:
        CHECK_FLOAT_EQ(lhs.to_f64(), rhs.to_f64());
        break;
      case ValueKind::kI8:
        CHECK_EQ(lhs.to_i8(), rhs.to_i8());
        break;
      case ValueKind::kI16:
        CHECK_EQ(lhs.to_i16(), rhs.to_i16());
        break;
      case ValueKind::kI32:
        CHECK_EQ(lhs.to_i32(), rhs.to_i32());
        break;
      case ValueKind::kI64:
        CHECK_EQ(lhs.to_i64(), rhs.to_i64());
        break;
      case ValueKind::kS128:
        CHECK_EQ(lhs.to_s128(), lhs.to_s128());
        break;
      case ValueKind::kRef:
      case ValueKind::kRefNull: {
        Tagged<Object> lhs_ref = *lhs.to_ref();
        Tagged<Object> rhs_ref = *rhs.to_ref();
        CHECK_EQ(IsNull(lhs_ref), IsNull(rhs_ref));
        CHECK_EQ(IsWasmNull(lhs_ref), IsWasmNull(rhs_ref));
        switch (lhs.type().heap_representation_non_shared()) {
          case HeapType::kFunc:
          case HeapType::kI31:
            CHECK_EQ(lhs_ref, rhs_ref);
            break;
          case HeapType::kNoFunc:
          case HeapType::kNone:
          case HeapType::kNoExn:
            CHECK(IsWasmNull(lhs_ref));
            CHECK(IsWasmNull(rhs_ref));
            break;
          case HeapType::kNoExtern:
            CHECK(IsNull(lhs_ref));
            CHECK(IsNull(rhs_ref));
            break;
          case HeapType::kExtern:
          case HeapType::kAny:
          case HeapType::kEq:
          case HeapType::kArray:
          case HeapType::kStruct:
            if (IsNullOrWasmNull(lhs_ref)) break;
            if (IsWasmStruct(lhs_ref)) {
              CheckStruct(lhs_ref, rhs_ref);
            } else if (IsWasmArray(lhs_ref)) {
              CheckArray(lhs_ref, rhs_ref);
            } else if (IsSmi(lhs_ref)) {
              CHECK_EQ(lhs_ref, rhs_ref);
            }
            break;
          default:
            CHECK(lhs.type().heap_type().is_index());
            if (IsWasmNull(lhs_ref)) break;
            uint32_t type_index = lhs.type().ref_index();
            if (module.has_signature(type_index)) {
              CHECK_EQ(lhs_ref, rhs_ref);
            } else if (module.has_struct(type_index)) {
              CheckStruct(lhs_ref, rhs_ref);
            } else if (module.has_array(type_index)) {
              CheckArray(lhs_ref, rhs_ref);
            } else {
              UNIMPLEMENTED();
            }
        }
        break;
      }
      default:
        UNIMPLEMENTED();
    }
  }
}

void FuzzIt(base::Vector<const uint8_t> data) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // Disable the optimizing compiler. The init expressions can be huge and might
  // produce long compilation times. The function is only used as a reference
  // and only run once, so use liftoff only as it allows much faster fuzzing.
  v8_flags.liftoff_only = true;

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  size_t expression_count = 0;
  base::Vector<const uint8_t> buffer =
      GenerateWasmModuleForInitExpressions(&zone, data, &expression_count);

  testing::SetupIsolateForWasmModule(i_isolate);
  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  bool valid = GetWasmEngine()->SyncValidate(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  CHECK(valid);
  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), &thrower,
      wire_bytes);
  CHECK(!compiled_module.is_null());
  CHECK(!thrower.error());
  thrower.Reset();
  CHECK(!i_isolate->has_exception());

  Handle<WasmModuleObject> module_object = compiled_module.ToHandleChecked();
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();
  CHECK_EQ(expression_count,
           module_object->native_module()->module()->num_declared_functions);

  for (size_t i = 0; i < expression_count; ++i) {
    char buffer[22];
    snprintf(buffer, sizeof buffer, "f%zu", i);
    // Execute corresponding function.
    auto function =
        Cast<WasmExportedFunction>(GetExport(i_isolate, instance, buffer));
    Handle<Object> undefined = i_isolate->factory()->undefined_value();
    Handle<Object> function_result =
        Execution::Call(i_isolate, function, undefined, 0, {})
            .ToHandleChecked();
    // Get global value.
    snprintf(buffer, sizeof buffer, "g%zu", i);
    auto global =
        Cast<WasmGlobalObject>(GetExport(i_isolate, instance, buffer));
    switch (global->type().kind()) {
      case ValueKind::kF32: {
        float global_val = global->GetF32();
        float func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = Cast<HeapNumber>(*function_result)->value();
        }
        CHECK_FLOAT_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kF64: {
        double global_val = global->GetF64();
        double func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = Cast<HeapNumber>(*function_result)->value();
        }
        CHECK_FLOAT_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kI32: {
        int32_t global_val = global->GetI32();
        int32_t func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsHeapNumber(*function_result));
          func_val = Cast<HeapNumber>(*function_result)->value();
        }
        CHECK_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kI64: {
        int64_t global_val = global->GetI64();
        int64_t func_val;
        if (IsSmi(*function_result)) {
          func_val = Smi::ToInt(*function_result);
        } else {
          CHECK(IsBigInt(*function_result));
          bool lossless;
          func_val = Cast<BigInt>(*function_result)->AsInt64(&lossless);
          CHECK(lossless);
        }
        CHECK_EQ(func_val, global_val);
        break;
      }
      case ValueKind::kRef:
      case ValueKind::kRefNull: {
        // For reference types the expectations are more limited.
        // Any struct.new would create a new object, so reference equality
        // comparisons will not work.
        DirectHandle<Object> global_val = global->GetRef();
        CHECK_EQ(IsUndefined(*global_val), IsUndefined(*function_result));
        CHECK_EQ(IsNullOrWasmNull(*global_val),
                 IsNullOrWasmNull(*function_result));
        if (!IsNullOrWasmNull(*global_val)) {
          if (IsSubtypeOf(global->type(), kWasmFuncRef,
                          module_object->module())) {
            // For any function the global should be an internal function
            // whose external function equals the call result. (The call goes
            // through JS conversions while the global is accessed directly.)
            CHECK(IsWasmFuncRef(*global_val));
            CHECK(
                WasmExportedFunction::IsWasmExportedFunction(*function_result));
            CHECK(*WasmInternalFunction::GetOrCreateExternal(handle(
                      Cast<WasmFuncRef>(*global_val)->internal(i_isolate),
                      i_isolate)) == *function_result);
          } else {
            // On arrays and structs, perform a deep comparison.
            DisallowGarbageCollection no_gc;
            WasmValue global_value =
                instance->trusted_data(i_isolate)->GetGlobalValue(
                    i_isolate, instance->module()->globals[i]);
            WasmValue func_value(function_result, global_value.type());
            CheckEquivalent(global_value, func_value, *module_object->module());
          }
        }
        break;
      }
      default:
        UNIMPLEMENTED();
    }
  }
}

}  // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzIt({data, size});
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing

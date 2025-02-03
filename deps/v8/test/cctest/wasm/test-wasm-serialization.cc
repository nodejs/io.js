// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include "include/v8-wasm.h"
#include "src/api/api-inl.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/code-serializer.h"
#include "src/utils/version.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-serialization.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8::internal::wasm {

// Approximate gtest TEST_F style, in case we adopt gtest.
class WasmSerializationTest {
 public:
  WasmSerializationTest() : zone_(&allocator_, ZONE_NAME) {
    // Don't call here if we move to gtest.
    SetUp();
  }

  static constexpr const char* kFunctionName = "increment";

  static void BuildWireBytes(Zone* zone, ZoneBuffer* buffer) {
    WasmModuleBuilder* builder = zone->New<WasmModuleBuilder>(zone);
    TestSignatures sigs;

    // Generate 3 functions, and export the last one with the name "increment".
    WasmFunctionBuilder* f;
    for (int i = 0; i < 3; ++i) {
      f = builder->AddFunction(sigs.i_i());
      uint8_t code[] = {WASM_LOCAL_GET(0), kExprI32Const, 1, kExprI32Add,
                        kExprEnd};
      f->EmitCode(code, sizeof(code));
    }
    builder->AddExport(base::CStrVector(kFunctionName), f);

    builder->WriteTo(buffer);
  }

  void ClearSerializedData() { serialized_bytes_ = {}; }

  void InvalidateVersion() {
    uint32_t* slot = reinterpret_cast<uint32_t*>(
        const_cast<uint8_t*>(serialized_bytes_.data()) +
        WasmSerializer::kVersionHashOffset);
    *slot = Version::Hash() + 1;
  }

  void InvalidateWireBytes() {
    memset(const_cast<uint8_t*>(wire_bytes_.data()), 0, wire_bytes_.size() / 2);
  }

  void PartlyDropTieringBudget() {
    serialized_bytes_ = {serialized_bytes_.data(),
                         serialized_bytes_.size() - 1};
  }

  MaybeHandle<WasmModuleObject> Deserialize(
      base::Vector<const char> source_url = {}) {
    return DeserializeNativeModule(
        CcTest::i_isolate(), base::VectorOf(serialized_bytes_),
        base::VectorOf(wire_bytes_), compile_imports_, source_url);
  }

  void DeserializeAndRun() {
    ErrorThrower thrower(CcTest::i_isolate(), "");
    Handle<WasmModuleObject> module_object;
    CHECK(Deserialize().ToHandle(&module_object));
    {
      DisallowGarbageCollection assume_no_gc;
      base::Vector<const uint8_t> deserialized_module_wire_bytes =
          module_object->native_module()->wire_bytes();
      CHECK_EQ(deserialized_module_wire_bytes.size(), wire_bytes_.size());
      CHECK_EQ(memcmp(deserialized_module_wire_bytes.begin(),
                      wire_bytes_.data(), wire_bytes_.size()),
               0);
    }
    Handle<WasmInstanceObject> instance =
        GetWasmEngine()
            ->SyncInstantiate(CcTest::i_isolate(), &thrower, module_object,
                              Handle<JSReceiver>::null(),
                              MaybeHandle<JSArrayBuffer>())
            .ToHandleChecked();
    Handle<Object> params[1] = {handle(Smi::FromInt(41), CcTest::i_isolate())};
    int32_t result = testing::CallWasmFunctionForTesting(
        CcTest::i_isolate(), instance, kFunctionName,
        base::ArrayVector(params));
    CHECK_EQ(42, result);
  }

  void CollectGarbage() {
    // Try hard to collect all garbage and will therefore also invoke all weak
    // callbacks of actually unreachable persistent handles.
    heap::InvokeMemoryReducingMajorGCs(CcTest::heap());
  }

  v8::MemorySpan<const uint8_t> wire_bytes() const { return wire_bytes_; }

  CompileTimeImports MakeCompileTimeImports() { return CompileTimeImports{}; }

 private:
  Zone* zone() { return &zone_; }

  void SetUp() {
    CcTest::InitIsolateOnce();
    ZoneBuffer buffer(&zone_);
    WasmSerializationTest::BuildWireBytes(zone(), &buffer);

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator =
        CcTest::i_isolate()->array_buffer_allocator();

    v8::Isolate* serialization_v8_isolate = v8::Isolate::New(create_params);
    Isolate* serialization_isolate =
        reinterpret_cast<Isolate*>(serialization_v8_isolate);
    ErrorThrower thrower(serialization_isolate, "");
    // Keep a weak pointer so we can check that the native module dies after
    // serialization (when the isolate is disposed).
    std::weak_ptr<NativeModule> weak_native_module;
    {
      v8::Isolate::Scope isolate_scope(serialization_v8_isolate);
      HandleScope scope(serialization_isolate);
      v8::Local<v8::Context> serialization_context =
          v8::Context::New(serialization_v8_isolate);
      serialization_context->Enter();

      auto enabled_features =
          WasmEnabledFeatures::FromIsolate(serialization_isolate);
      MaybeHandle<WasmModuleObject> maybe_module_object =
          GetWasmEngine()->SyncCompile(
              serialization_isolate, enabled_features, MakeCompileTimeImports(),
              &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
      Handle<WasmModuleObject> module_object =
          maybe_module_object.ToHandleChecked();
      weak_native_module = module_object->shared_native_module();
      // Check that the native module exists at this point.
      CHECK(weak_native_module.lock());

      v8::Local<v8::Object> v8_module_obj =
          v8::Utils::ToLocal(Cast<JSObject>(module_object));
      CHECK(v8_module_obj->IsWasmModuleObject());

      v8::Local<v8::WasmModuleObject> v8_module_object =
          v8_module_obj.As<v8::WasmModuleObject>();
      v8::CompiledWasmModule compiled_module =
          v8_module_object->GetCompiledModule();
      v8::MemorySpan<const uint8_t> uncompiled_bytes =
          compiled_module.GetWireBytesRef();
      uint8_t* bytes_copy =
          zone()->AllocateArray<uint8_t>(uncompiled_bytes.size());
      memcpy(bytes_copy, uncompiled_bytes.data(), uncompiled_bytes.size());
      wire_bytes_ = {bytes_copy, uncompiled_bytes.size()};

      // Run the code until tier-up (of the single function) was observed.
      Handle<WasmInstanceObject> instance =
          GetWasmEngine()
              ->SyncInstantiate(serialization_isolate, &thrower, module_object,
                                {}, {})
              .ToHandleChecked();
      CHECK_EQ(0, data_.size);
      while (data_.size == 0) {
        testing::CallWasmFunctionForTesting(serialization_isolate, instance,
                                            kFunctionName, {});
        data_ = compiled_module.Serialize();
      }
      CHECK_LT(0, data_.size);
    }
    // Dispose of serialization isolate to destroy the reference to the
    // NativeModule, which removes it from the module cache in the wasm engine
    // and forces de-serialization in the new isolate.
    serialization_v8_isolate->Dispose();

    // Busy-wait for the NativeModule to really die. Background threads might
    // temporarily keep it alive (happens very rarely, see
    // https://crbug.com/v8/10148).
    while (weak_native_module.lock()) {
    }

    serialized_bytes_ = {data_.buffer.get(), data_.size};

    v8::HandleScope new_scope(CcTest::isolate());
    v8::Local<v8::Context> deserialization_context =
        v8::Context::New(CcTest::isolate());
    deserialization_context->Enter();
  }

  v8::internal::AccountingAllocator allocator_;
  Zone zone_;
  // TODO(14179): Add tests for de/serializing modules with compile-time
  // imports.
  CompileTimeImports compile_imports_;
  v8::OwnedBuffer data_;
  v8::MemorySpan<const uint8_t> wire_bytes_ = {nullptr, 0};
  v8::MemorySpan<const uint8_t> serialized_bytes_ = {nullptr, 0};
  FlagScope<int> tier_up_quickly_{&v8_flags.wasm_tiering_budget, 1000};
};

TEST(DeserializeValidModule) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    test.DeserializeAndRun();
  }
  test.CollectGarbage();
}

TEST(DeserializeWithSourceUrl) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    const std::string url = "http://example.com/example.wasm";
    Handle<WasmModuleObject> module_object;
    CHECK(test.Deserialize(base::VectorOf(url)).ToHandle(&module_object));
    Tagged<String> url_str = Cast<String>(module_object->script()->name());
    CHECK_EQ(url, url_str->ToCString().get());
  }
  test.CollectGarbage();
}

TEST(DeserializeMismatchingVersion) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    test.InvalidateVersion();
    CHECK(test.Deserialize().is_null());
  }
  test.CollectGarbage();
}

TEST(DeserializeNoSerializedData) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    test.ClearSerializedData();
    CHECK(test.Deserialize().is_null());
  }
  test.CollectGarbage();
}

TEST(DeserializeWireBytesAndSerializedDataInvalid) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    test.InvalidateVersion();
    test.InvalidateWireBytes();
    CHECK(test.Deserialize().is_null());
  }
  test.CollectGarbage();
}

bool False(v8::Local<v8::Context> context, v8::Local<v8::String> source) {
  return false;
}

TEST(BlockWasmCodeGenAtDeserialization) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    CcTest::isolate()->SetAllowWasmCodeGenerationCallback(False);
    CHECK(test.Deserialize().is_null());
  }
  test.CollectGarbage();
}

UNINITIALIZED_TEST(CompiledWasmModulesTransfer) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  ZoneBuffer buffer(&zone);
  WasmSerializationTest::BuildWireBytes(&zone, &buffer);

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* from_isolate = v8::Isolate::New(create_params);
  std::vector<v8::CompiledWasmModule> store;
  std::shared_ptr<NativeModule> original_native_module;
  {
    v8::Isolate::Scope isolate_scope(from_isolate);
    v8::HandleScope scope(from_isolate);
    LocalContext env(from_isolate);

    Isolate* from_i_isolate = reinterpret_cast<Isolate*>(from_isolate);
    testing::SetupIsolateForWasmModule(from_i_isolate);
    ErrorThrower thrower(from_i_isolate, "TestCompiledWasmModulesTransfer");
    auto enabled_features = WasmEnabledFeatures::FromIsolate(from_i_isolate);
    MaybeHandle<WasmModuleObject> maybe_module_object =
        GetWasmEngine()->SyncCompile(
            from_i_isolate, enabled_features, CompileTimeImports{}, &thrower,
            ModuleWireBytes(buffer.begin(), buffer.end()));
    Handle<WasmModuleObject> module_object =
        maybe_module_object.ToHandleChecked();
    v8::Local<v8::WasmModuleObject> v8_module =
        v8::Local<v8::WasmModuleObject>::Cast(
            v8::Utils::ToLocal(Cast<JSObject>(module_object)));
    store.push_back(v8_module->GetCompiledModule());
    original_native_module = module_object->shared_native_module();
  }

  {
    v8::Isolate* to_isolate = v8::Isolate::New(create_params);
    {
      v8::Isolate::Scope isolate_scope(to_isolate);
      v8::HandleScope scope(to_isolate);
      LocalContext env(to_isolate);

      v8::MaybeLocal<v8::WasmModuleObject> transferred_module =
          v8::WasmModuleObject::FromCompiledModule(to_isolate, store[0]);
      CHECK(!transferred_module.IsEmpty());
      DirectHandle<WasmModuleObject> module_object = Cast<WasmModuleObject>(
          v8::Utils::OpenDirectHandle(*transferred_module.ToLocalChecked()));
      std::shared_ptr<NativeModule> transferred_native_module =
          module_object->shared_native_module();
      CHECK_EQ(original_native_module, transferred_native_module);
    }
    to_isolate->Dispose();
  }
  original_native_module.reset();
  from_isolate->Dispose();
}

TEST(TierDownAfterDeserialization) {
  WasmSerializationTest test;

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Handle<WasmModuleObject> module_object;
  CHECK(test.Deserialize().ToHandle(&module_object));

  auto* native_module = module_object->native_module();
  CHECK_EQ(3, native_module->module()->functions.size());
  WasmCodeRefScope code_ref_scope;
  // The deserialized code must be TurboFan (we wait for tier-up before
  // serializing).
  auto* turbofan_code = native_module->GetCode(2);
  CHECK_NOT_NULL(turbofan_code);
  CHECK_EQ(ExecutionTier::kTurbofan, turbofan_code->tier());

  GetWasmEngine()->EnterDebuggingForIsolate(isolate);

  // Entering debugging should delete all code, so that debug code gets compiled
  // lazily.
  CHECK_NULL(native_module->GetCode(0));
}

TEST(SerializeLiftoffModuleFails) {
  // Make sure that no function is tiered up to TurboFan.
  if (!v8_flags.liftoff) return;
  FlagScope<bool> no_tier_up(&v8_flags.wasm_tier_up, false);
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, "test_zone");

  CcTest::InitIsolateOnce();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  ZoneBuffer wire_bytes_buffer(&zone);
  WasmSerializationTest::BuildWireBytes(&zone, &wire_bytes_buffer);

  ErrorThrower thrower(isolate, "Test");
  MaybeHandle<WasmModuleObject> maybe_module_object =
      GetWasmEngine()->SyncCompile(
          isolate, WasmEnabledFeatures::All(), CompileTimeImports{}, &thrower,
          ModuleWireBytes(wire_bytes_buffer.begin(), wire_bytes_buffer.end()));
  DirectHandle<WasmModuleObject> module_object =
      maybe_module_object.ToHandleChecked();

  NativeModule* native_module = module_object->native_module();
  WasmSerializer wasm_serializer(native_module);
  size_t buffer_size = wasm_serializer.GetSerializedNativeModuleSize();
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
  // Serialization is expected to fail if there is no TurboFan function to
  // serialize.
  CHECK(!wasm_serializer.SerializeNativeModule({buffer.get(), buffer_size}));
}

TEST(SerializeTieringBudget) {
  WasmSerializationTest test;

  Isolate* isolate = CcTest::i_isolate();
  v8::OwnedBuffer serialized_bytes;
  uint32_t mock_budget[3]{1, 2, 3};
  {
    HandleScope scope(isolate);
    Handle<WasmModuleObject> module_object;
    CHECK(test.Deserialize().ToHandle(&module_object));

    auto* native_module = module_object->native_module();
    memcpy(native_module->tiering_budget_array(), mock_budget,
           arraysize(mock_budget) * sizeof(uint32_t));
    v8::Local<v8::Object> v8_module_obj =
        v8::Utils::ToLocal(Cast<JSObject>(module_object));
    CHECK(v8_module_obj->IsWasmModuleObject());

    v8::Local<v8::WasmModuleObject> v8_module_object =
        v8_module_obj.As<v8::WasmModuleObject>();
    serialized_bytes = v8_module_object->GetCompiledModule().Serialize();

    // Change one entry in the tiering budget after serialization to make sure
    // the module gets deserialized and not just loaded from the module cache.
    native_module->tiering_budget_array()[0]++;
  }
  // We need to invoke GC without stack, otherwise some objects may survive.
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      isolate->heap());
  test.CollectGarbage();
  HandleScope scope(isolate);
  Handle<WasmModuleObject> module_object;
  CompileTimeImports compile_imports = test.MakeCompileTimeImports();
  CHECK(
      DeserializeNativeModule(
          isolate,
          base::VectorOf(serialized_bytes.buffer.get(), serialized_bytes.size),
          base::VectorOf(test.wire_bytes()), compile_imports, {})
          .ToHandle(&module_object));

  auto* native_module = module_object->native_module();
  for (size_t i = 0; i < arraysize(mock_budget); ++i) {
    CHECK_EQ(mock_budget[i], native_module->tiering_budget_array()[i]);
  }
}

TEST(DeserializeTieringBudgetPartlyMissing) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());
    test.PartlyDropTieringBudget();
    CHECK(test.Deserialize().is_null());
  }
  test.CollectGarbage();
}

TEST(SerializationFailsOnChangedFlags) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());

    FlagScope<bool> no_bounds_checks(&v8_flags.wasm_bounds_checks, false);
    CHECK(test.Deserialize().is_null());

    FlagScope<bool> bounds_checks(&v8_flags.wasm_bounds_checks, true);
    CHECK(!test.Deserialize().is_null());
  }
}

TEST(SerializationFailsOnChangedFeatures) {
  WasmSerializationTest test;
  {
    HandleScope scope(CcTest::i_isolate());

    CcTest::isolate()->SetWasmImportedStringsEnabledCallback(
        [](auto) { return true; });
    CHECK(test.Deserialize().is_null());

    CcTest::isolate()->SetWasmImportedStringsEnabledCallback(
        [](auto) { return false; });
    CHECK(!test.Deserialize().is_null());
  }
}

TEST(DeserializeIndirectCallWithDifferentCanonicalId) {
  // This test compiles and serializes a module with an indirect call, then
  // resets the type canonicalizer, compiles another module, and then
  // deserializes the original module. This ensures that a different canonical
  // signature ID is used for the indirect call.
  // We then call the deserialized module to check that the right canonical
  // signature ID is being used.

  // Compile with Turbofan right away.
  FlagScope<bool> no_liftoff{&v8_flags.liftoff, false};
  FlagScope<bool> no_lazy_compilation{&v8_flags.wasm_lazy_compilation, false};
  FlagScope<bool> expose_gc{&v8_flags.expose_gc, true};

  i::Isolate* i_isolate = CcTest::InitIsolateOnce();
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  HandleScope scope(i_isolate);

  // Build a small module with an indirect call.
  ZoneBuffer buffer(&zone);
  {
    WasmModuleBuilder builder{&zone};
    TestSignatures sigs;

    // Add the "call_indirect" function which calls table0[0].
    uint32_t sig_id = builder.AddSignature(sigs.i_i(), true);
    WasmFunctionBuilder* f = builder.AddFunction(sig_id);
    uint8_t code[] = {
        // (i) => i != 0 ? f(i-1) : 42
        WASM_IF_ELSE_I(
            // cond:
            WASM_LOCAL_GET(0),
            // if_true:
            WASM_CALL_INDIRECT(SIG_INDEX(sig_id),
                               WASM_I32_SUB(WASM_LOCAL_GET(0), WASM_ONE),
                               WASM_ZERO),
            // if_false:
            WASM_I32V_1(42)),
        WASM_END};
    f->EmitCode(code, sizeof(code));
    builder.AddExport(base::CStrVector("call_indirect"), f);
    // Add a function table.
    uint32_t table_id = builder.AddTable(kWasmFuncRef, 1);
    builder.SetIndirectFunction(
        table_id, 0, f->func_index(),
        WasmModuleBuilder::WasmElemSegment::kRelativeToImports);
    // Write the final module into {buffer}.
    builder.WriteTo(&buffer);
  }

  // Instantiate the module and serialize it.
  // Keep a weak pointer so we can check that the original native module died.
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  std::weak_ptr<NativeModule> weak_native_module;
  v8::OwnedBuffer serialized_module;
  uint32_t canonical_sig_id_before_serialization;
  {
    ErrorThrower thrower(i_isolate, "");

    {
      v8::Isolate::Scope isolate_scope(v8_isolate);
      HandleScope scope(i_isolate);
      v8::Local<v8::Context> serialization_context =
          v8::Context::New(v8_isolate);
      serialization_context->Enter();

      Handle<WasmModuleObject> module_object =
          GetWasmEngine()
              ->SyncCompile(i_isolate, enabled_features, CompileTimeImports{},
                            &thrower,
                            ModuleWireBytes(buffer.begin(), buffer.end()))
              .ToHandleChecked();
      weak_native_module = module_object->shared_native_module();

      // Retrieve the canonicalized signature ID.
      const std::vector<uint32_t>& canonical_type_ids =
          module_object->native_module()
              ->module()
              ->isorecursive_canonical_type_ids;
      CHECK_EQ(1, canonical_type_ids.size());
      canonical_sig_id_before_serialization = canonical_type_ids[0];

      // Check that the embedded constant in the code is right.
      WasmCodeRefScope code_ref_scope;
      WasmCode* code = module_object->native_module()->GetCode(0);
      RelocIterator reloc_it{
          code->instructions(), code->reloc_info(), code->constant_pool(),
          RelocInfo::ModeMask(RelocInfo::WASM_CANONICAL_SIG_ID)};
      CHECK(!reloc_it.done());
      CHECK_EQ(canonical_sig_id_before_serialization,
               reloc_it.rinfo()->wasm_canonical_sig_id());
      reloc_it.next();
      CHECK(reloc_it.done());

      // Convert to API objects and serialize.
      v8::Local<v8::WasmModuleObject> v8_module_object =
          v8::Utils::ToLocal(module_object);
      serialized_module = v8_module_object->GetCompiledModule().Serialize();
    }

    CHECK_LT(0, serialized_module.size);

    // Run GC until the NativeModule died. Add a manual timeout of 60 seconds to
    // get a better error message than just a test timeout if this fails.
    const auto start_time = std::chrono::steady_clock::now();
    const auto end_time = start_time + std::chrono::seconds(60);
    while (weak_native_module.lock()) {
      v8_isolate->RequestGarbageCollectionForTesting(
          v8::Isolate::kFullGarbageCollection);
      if (std::chrono::steady_clock::now() > end_time) {
        FATAL("NativeModule did not die within 60 seconds");
      }
    }
  }

  // Clear canonicalized types, then compile another module which adds a
  // canonical type at the same index we used in the previous module.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  {
    ZoneBuffer buffer(&zone);
    WasmModuleBuilder builder{&zone};
    TestSignatures sigs;

    uint32_t sig_id = builder.AddSignature(sigs.v_v(), true);
    WasmFunctionBuilder* f = builder.AddFunction(sig_id);
    f->EmitByte(kExprEnd);
    builder.WriteTo(&buffer);
    ErrorThrower thrower(i_isolate, "");
    GetWasmEngine()
        ->SyncCompile(i_isolate, enabled_features, CompileTimeImports{},
                      &thrower, ModuleWireBytes(buffer.begin(), buffer.end()))
        .ToHandleChecked();
  }

  // Now deserialize the previous module.
  uint32_t canonical_sig_id_after_deserialization =
      canonical_sig_id_before_serialization + 1;
  {
    v8::Local<v8::Context> deserialization_context =
        v8::Context::New(CcTest::isolate());
    deserialization_context->Enter();
    ErrorThrower thrower(CcTest::i_isolate(), "");
    base::Vector<const char> kNoSourceUrl;
    Handle<WasmModuleObject> module_object =
        DeserializeNativeModule(CcTest::i_isolate(),
                                base::VectorOf(serialized_module.buffer.get(),
                                               serialized_module.size),
                                base::VectorOf(buffer), CompileTimeImports{},
                                kNoSourceUrl)
            .ToHandleChecked();

    // Check that the signature ID got canonicalized to index 1.
    const std::vector<uint32_t>& canonical_type_ids =
        module_object->native_module()
            ->module()
            ->isorecursive_canonical_type_ids;
    CHECK_EQ(1, canonical_type_ids.size());
    CHECK_EQ(canonical_sig_id_after_deserialization, canonical_type_ids[0]);

    // Check that the embedded constant in the code is right.
    WasmCodeRefScope code_ref_scope;
    WasmCode* code = module_object->native_module()->GetCode(0);
    RelocIterator reloc_it{
        code->instructions(), code->reloc_info(), code->constant_pool(),
        RelocInfo::ModeMask(RelocInfo::WASM_CANONICAL_SIG_ID)};
    CHECK(!reloc_it.done());
    CHECK_EQ(canonical_sig_id_after_deserialization,
             reloc_it.rinfo()->wasm_canonical_sig_id());
    reloc_it.next();
    CHECK(reloc_it.done());

    // Now call the function.
    Handle<WasmInstanceObject> instance =
        GetWasmEngine()
            ->SyncInstantiate(CcTest::i_isolate(), &thrower, module_object,
                              Handle<JSReceiver>::null(),
                              MaybeHandle<JSArrayBuffer>())
            .ToHandleChecked();
    Handle<Object> params[1] = {handle(Smi::FromInt(1), i_isolate)};
    int32_t result = testing::CallWasmFunctionForTesting(
        i_isolate, instance, "call_indirect", base::ArrayVector(params));
    CHECK_EQ(42, result);
  }
}

}  // namespace v8::internal::wasm

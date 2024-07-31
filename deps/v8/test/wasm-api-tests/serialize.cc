// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate.h"
#include "src/wasm/c-api.h"
#include "test/wasm-api-tests/wasm-api-test.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

bool g_callback_called;

own<Trap> Callback(const Val args[], Val results[]) {
  g_callback_called = true;
  return nullptr;
}

}  // namespace

TEST_F(WasmCapiTest, Serialize) {
  FunctionSig sig(0, 0, nullptr);
  uint32_t callback_index =
      builder()->AddImport(base::CStrVector("callback"), &sig);
  uint8_t code[] = {WASM_CALL_FUNCTION0(callback_index)};
  AddExportedFunction(base::CStrVector("run"), code, sizeof(code), &sig);
  Compile();

  vec<byte_t> serialized = module()->serialize();
  EXPECT_TRUE(serialized);  // Serialization succeeded.

  // We reset the module and collect it to make sure the NativeModuleCache does
  // not contain it anymore. Otherwise deserialization will not happen.
  ResetModule();
  {
    Isolate* isolate =
        reinterpret_cast<::wasm::StoreImpl*>(store())->i_isolate();
    // This method might be called on a thread that's not bound to any Isolate
    // and thus pointer compression schemes might have cage base value unset.
    // Ensure cage bases are initialized so that the V8 heap can be accessed.
    i::PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
    Heap* heap = isolate->heap();
    heap->PreciseCollectAllGarbage(GCFlag::kForced,
                                   GarbageCollectionReason::kTesting);
    heap->PreciseCollectAllGarbage(GCFlag::kForced,
                                   GarbageCollectionReason::kTesting);
  }
  own<Module> deserialized = Module::deserialize(store(), serialized);

  // Try to serialize the module again. This can fail if deserialization does
  // not set up a clean state.
  deserialized->serialize();

  own<FuncType> callback_type =
      FuncType::make(ownvec<ValType>::make(), ownvec<ValType>::make());
  own<Func> callback = Func::make(store(), callback_type.get(), Callback);
  Extern* imports[] = {callback.get()};

  own<Instance> instance = Instance::make(store(), deserialized.get(), imports);
  ownvec<Extern> exports = instance->exports();
  Func* run = exports[0]->func();
  g_callback_called = false;
  run->call();
  EXPECT_TRUE(g_callback_called);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

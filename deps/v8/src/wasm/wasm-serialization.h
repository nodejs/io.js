// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_SERIALIZATION_H_
#define V8_WASM_WASM_SERIALIZATION_H_

#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-objects.h"

namespace v8::internal::wasm {

// Support for serializing WebAssembly {NativeModule} objects. This class takes
// a snapshot of the module state at instantiation, and other code that modifies
// the module after that won't affect the serialized result.
class V8_EXPORT_PRIVATE WasmSerializer {
 public:
  explicit WasmSerializer(NativeModule* native_module);

  // Measure the required buffer size needed for serialization.
  size_t GetSerializedNativeModuleSize() const;

  // Serialize the {NativeModule} into the provided {buffer}. Returns true on
  // success and false if the given buffer it too small for serialization.
  bool SerializeNativeModule(base::Vector<uint8_t> buffer) const;

  // The data header consists of uint32_t-sized entries (see {WriteVersion}):
  // [0] magic number
  // [1] version hash
  // [2] supported CPU features
  // [3] flag hash
  // [4] enabled features (via flags and OT)
  // ...  number of functions
  // ... serialized functions
  static constexpr size_t kMagicNumberOffset = 0;
  static constexpr size_t kVersionHashOffset = kMagicNumberOffset + kUInt32Size;
  static constexpr size_t kSupportedCPUFeaturesOffset =
      kVersionHashOffset + kUInt32Size;
  static constexpr size_t kFlagHashOffset =
      kSupportedCPUFeaturesOffset + kUInt32Size;
  static constexpr size_t kHeaderSize = 5 * kUInt32Size;

 private:
  NativeModule* native_module_;
  // The {WasmCodeRefScope} keeps the pointers in {code_table_} alive.
  WasmCodeRefScope code_ref_scope_;
  std::vector<WasmCode*> code_table_;
  std::vector<WellKnownImport> import_statuses_;
};

// Support for deserializing WebAssembly {NativeModule} objects.
// Checks the version header of the data against the current version.
bool IsSupportedVersion(base::Vector<const uint8_t> data,
                        WasmEnabledFeatures enabled_features);

// Deserializes the given data to create a Wasm module object.
V8_EXPORT_PRIVATE MaybeHandle<WasmModuleObject> DeserializeNativeModule(
    Isolate*, base::Vector<const uint8_t> data,
    base::Vector<const uint8_t> wire_bytes,
    const CompileTimeImports& compile_imports,
    base::Vector<const char> source_url);

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_SERIALIZATION_H_

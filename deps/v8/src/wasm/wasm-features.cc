// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-features.h"

#include "src/execution/isolate-inl.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {
namespace wasm {

// static
WasmEnabledFeatures WasmEnabledFeatures::FromFlags() {
  WasmEnabledFeatures features = WasmEnabledFeatures::None();

#if V8_ENABLE_DRUMBRAKE
  // The only Wasm experimental features supported by DrumBrake is the legacy
  // exception handling.
  if (v8_flags.wasm_jitless) {
    features.Add(WasmEnabledFeature::legacy_eh);
  }
#endif  // V8_ENABLE_DRUMBRAKE

#define CHECK_FEATURE_FLAG(feat, ...)                              \
  if (!v8_flags.wasm_jitless && v8_flags.experimental_wasm_##feat) \
    features.Add(WasmEnabledFeature::feat);
  FOREACH_WASM_FEATURE_FLAG(CHECK_FEATURE_FLAG)
#undef CHECK_FEATURE_FLAG
  return features;
}

// static
WasmEnabledFeatures WasmEnabledFeatures::FromIsolate(Isolate* isolate) {
  return FromContext(isolate, isolate->native_context());
}

// static
WasmEnabledFeatures WasmEnabledFeatures::FromContext(
    Isolate* isolate, Handle<NativeContext> context) {
  WasmEnabledFeatures features = WasmEnabledFeatures::FromFlags();
  if (!v8_flags.wasm_jitless) {
    if (isolate->IsWasmStringRefEnabled(context)) {
      features.Add(WasmEnabledFeature::stringref);
    }
    if (isolate->IsWasmInliningEnabled(context)) {
      features.Add(WasmEnabledFeature::inlining);
    }
    if (isolate->IsWasmImportedStringsEnabled(context)) {
      features.Add(WasmEnabledFeature::imported_strings);
    }
    if (isolate->IsWasmJSPIEnabled(context)) {
      features.Add(WasmEnabledFeature::jspi);
      features.Add(WasmEnabledFeature::type_reflection);
    }
  }
  // This space intentionally left blank for future Wasm origin trials.
  return features;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

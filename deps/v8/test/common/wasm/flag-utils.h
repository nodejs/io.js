// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_COMMON_WASM_FLAG_UTILS_H
#define V8_TEST_COMMON_WASM_FLAG_UTILS_H

#include "src/wasm/wasm-features.h"
#include "test/common/flag-utils.h"

namespace v8::internal::wasm {

#define EXPERIMENTAL_FLAG_SCOPE(flag) FLAG_SCOPE(experimental_wasm_##flag)

class V8_NODISCARD WasmFeatureScope {
 public:
  explicit WasmFeatureScope(WasmEnabledFeatures* features,
                            WasmEnabledFeature feature, bool val = true)
      : prev_(features->contains(feature)),
        feature_(feature),
        features_(features) {
    set(val);
  }
  ~WasmFeatureScope() { set(prev_); }

 private:
  void set(bool val) {
    if (val) {
      features_->Add(feature_);
    } else {
      features_->Remove(feature_);
    }
  }

  bool const prev_;
  WasmEnabledFeature const feature_;
  WasmEnabledFeatures* const features_;
};

#define WASM_FEATURE_SCOPE(feat)                          \
  WasmFeatureScope feat##_scope(&this->enabled_features_, \
                                WasmEnabledFeature::feat)

#define WASM_FEATURE_SCOPE_VAL(feat, val)                 \
  WasmFeatureScope feat##_scope(&this->enabled_features_, \
                                WasmEnabledFeature::feat, val)

}  // namespace v8::internal::wasm

#endif  // V8_TEST_COMMON_WASM_FLAG_UTILS_H

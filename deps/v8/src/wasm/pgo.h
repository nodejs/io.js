// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_PGO_H_
#define V8_WASM_PGO_H_

#include <vector>

#include "src/base/vector.h"

namespace v8::internal::wasm {

struct WasmModule;

class ProfileInformation {
 public:
  ProfileInformation(std::vector<uint32_t> executed_functions,
                     std::vector<uint32_t> tiered_up_functions)
      : executed_functions_(std::move(executed_functions)),
        tiered_up_functions_(std::move(tiered_up_functions)) {}

  // Disallow copying (not needed, so most probably a bug).
  ProfileInformation(const ProfileInformation&) = delete;
  ProfileInformation& operator=(const ProfileInformation&) = delete;

  base::Vector<const uint32_t> executed_functions() const {
    return base::VectorOf(executed_functions_);
  }
  base::Vector<const uint32_t> tiered_up_functions() const {
    return base::VectorOf(tiered_up_functions_);
  }

 private:
  const std::vector<uint32_t> executed_functions_;
  const std::vector<uint32_t> tiered_up_functions_;
};

void DumpProfileToFile(const WasmModule* module,
                       base::Vector<const uint8_t> wire_bytes,
                       std::atomic<uint32_t>* tiering_budget_array);

V8_WARN_UNUSED_RESULT std::unique_ptr<ProfileInformation> LoadProfileFromFile(
    const WasmModule* module, base::Vector<const uint8_t> wire_bytes);

}  // namespace v8::internal::wasm

#endif  // V8_WASM_PGO_H_

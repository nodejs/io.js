// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/base/logging.h"
#include "src/execution/isolate-inl.h"
#include "src/trap-handler/trap-handler.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#if V8_ENABLE_WEBASSEMBLY
SaveAndClearThreadInWasmFlag::SaveAndClearThreadInWasmFlag(Isolate* isolate)
    : isolate_(isolate) {
  DCHECK(isolate_);
  if (trap_handler::IsTrapHandlerEnabled() && trap_handler::IsThreadInWasm()) {
    thread_was_in_wasm_ = true;
    trap_handler::ClearThreadInWasm();
  }
}

SaveAndClearThreadInWasmFlag::~SaveAndClearThreadInWasmFlag() {
  if (thread_was_in_wasm_ && !isolate_->has_exception()) {
    trap_handler::SetThreadInWasm();
  }
}
#else
SaveAndClearThreadInWasmFlag::SaveAndClearThreadInWasmFlag(Isolate* isolate) {}

SaveAndClearThreadInWasmFlag::~SaveAndClearThreadInWasmFlag() = default;
#endif

}  // namespace internal
}  // namespace v8

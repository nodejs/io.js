// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// https://tc39.es/proposal-source-phase-imports/#sec-get-%abstractmodulesource%.prototype.@@tostringtag
BUILTIN(AbstractModuleSourceToStringTag) {
  HandleScope scope(isolate);
  // 1. Let O be the this value.
  DirectHandle<Object> receiver = args.receiver();

  // 2. If O is not an Object, return undefined.
  if (!IsJSReceiver(*receiver)) {
    return *isolate->factory()->undefined_value();
  }
  // 3. Let sourceNameResult be Completion(HostGetModuleSourceName(O)).
  // 4. If sourceNameResult is an abrupt completion, return undefined.
  // 5. Let name be ! sourceNameResult.
  // 6. Assert: name is a String.
  // 7. Return name.

#if V8_ENABLE_WEBASSEMBLY
  // https://webassembly.github.io/esm-integration/js-api/index.html#hostgetmodulesourcename
  // Whenever a WebAssembly Module object is provided with a [[Module]] internal
  // slot, the string "WebAssembly.Module" is always returned.
  if (IsWasmModuleObject(*receiver)) {
    return *isolate->factory()->WebAssemblyModule_string();
  }
#endif
  // TODO(42204365): Implement host hook.
  return *isolate->factory()->undefined_value();
}

}  // namespace internal
}  // namespace v8

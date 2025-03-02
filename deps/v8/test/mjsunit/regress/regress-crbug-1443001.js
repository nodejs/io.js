// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-async-hooks --strict-termination-checks

try {
  let hook = async_hooks.createHook({
    init: function() {
      d8.terminate();
      Object.getOwnPropertyNames({});
    }
  });
  hook.enable();
  WebAssembly.compile(959070);
} catch(e) {
}

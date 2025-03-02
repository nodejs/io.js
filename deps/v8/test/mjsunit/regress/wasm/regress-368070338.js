// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-test-streaming

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestAsyncCompileWithSharedMemory() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let except = builder.addMemory(1, 1, true);
  assertPromiseResult(WebAssembly.compile(builder.toBuffer()));
})();

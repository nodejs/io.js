// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: We do not enable --wasm-staging here.
// %WasmGenerateRandomModule is supposed to also work without that, such that it
// can be used by fuzzers.

// Flags: --allow-natives-syntax

(function TestGenerateRandomModule() {
  print(arguments.callee.name);
  assertInstanceof(%WasmGenerateRandomModule(), WebAssembly.Module);
  assertInstanceof(%WasmGenerateRandomModule(4), WebAssembly.Module);
  assertInstanceof(%WasmGenerateRandomModule(4.2), WebAssembly.Module);
  assertInstanceof(
      %WasmGenerateRandomModule(new Uint8Array([11, 27])), WebAssembly.Module);
  assertInstanceof(
      %WasmGenerateRandomModule(new Uint8Array([11, 27]).buffer),
      WebAssembly.Module);
})();

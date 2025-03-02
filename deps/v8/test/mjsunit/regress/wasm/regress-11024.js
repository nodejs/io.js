// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The test needs --no-liftoff because we can't serialize and deserialize
// Liftoff code.
// Flags: --allow-natives-syntax --expose-gc --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const num_functions = 3;

function create_builder() {
  const builder = new WasmModuleBuilder();
  for (let i = 0; i < num_functions; ++i) {
    builder.addFunction('f' + i, kSig_i_v)
        .addBody(wasmI32Const(i))
        .exportFunc();
  }
  return builder;
}

const wire_bytes = create_builder().toBuffer();

const serialized = (() => {
  const module = new WebAssembly.Module(wire_bytes);
  const instance = new WebAssembly.Instance(module);
  // Run one function so that serialization happens.
  instance.exports.f2();
  return %SerializeWasmModule(module);
})();

// Collect the compiled module, to avoid sharing of the NativeModule.
gc();

const module = %DeserializeWasmModule(serialized, wire_bytes);
%SerializeWasmModule(module);

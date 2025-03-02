// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The test needs --no-liftoff because we can't serialize and deserialize
// Liftoff code.
// Flags: --allow-natives-syntax --wasm-lazy-compilation --expose-gc
// Flags: --no-liftoff --no-wasm-native-module-cache

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const num_functions = 3;

function create_builder() {
  const builder = new WasmModuleBuilder();
  builder.addImport("foo", "bar", kSig_i_v);
  for (let i = 0; i < num_functions; ++i) {
    builder.addFunction('f' + i, kSig_i_v)
        .addBody(wasmI32Const(i))
        .exportFunc();
  }
  return builder;
}

const wire_bytes = create_builder().toBuffer();

function serializeModule() {
  const module = new WebAssembly.Module(wire_bytes);
  // Run one function so that serialization happens.
  let instance = new WebAssembly.Instance(module, {foo: {bar: () => 1}});
  instance.exports.f2();
  const buff = %SerializeWasmModule(module);
  return buff;
};

const serialized_module = serializeModule();

(function testSerializedModule() {
  print(arguments.callee.name);
  const module = %DeserializeWasmModule(serialized_module, wire_bytes);

  const instance = new WebAssembly.Instance(module, {foo: {bar: () => 1}});
  assertEquals(0, instance.exports.f0());
  assertEquals(1, instance.exports.f1());
})();

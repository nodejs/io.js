// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-dynamic-tiering --liftoff
// Flags: --no-wasm-native-module-cache
// Make the test faster:
// Flags: --wasm-tiering-budget=1000

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
  let instance = new WebAssembly.Instance(module, {foo: {bar: () => 1}});
  %WasmTierUpFunction(instance.exports.f1);
  // Execute {f2} once, so that the module knows that this is a used function.
  instance.exports.f2();
  const buff = %SerializeWasmModule(module);
  return buff;
};

const serialized_module = serializeModule();

(function testSerializedModule() {
  print(arguments.callee.name);
  const module = %DeserializeWasmModule(serialized_module, wire_bytes);

  const instance = new WebAssembly.Instance(module, {foo: {bar: () => 1}});

  assertTrue(%IsTurboFanFunction(instance.exports.f1));
  // Busy-wait for `f2` to be compiled with Liftoff.
  while (!%IsLiftoffFunction(instance.exports.f2)) {}
  assertTrue(
      !%IsLiftoffFunction(instance.exports.f0) &&
      !%IsTurboFanFunction(instance.exports.f0));
})();

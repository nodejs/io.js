// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestMemory64HugeIndex() {
  const builder = new WasmModuleBuilder();
  builder.addMemory64(1, undefined);
  builder.addMemory64(92438, undefined);

  builder.addFunction("main", kSig_i_i).exportFunc().addBody([
    ...wasmI64Const(-1n),
    kExprI32LoadMem16S, 65, 1, 0,
  ]);

  let instance;
  try {
    instance = builder.instantiate({});
  } catch (e) {
    assertContains('Out of memory', e.message);
    return;
  }
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0));
  %WasmTierUpFunction(instance.exports.main);
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0));
})();

(function TestMemory32HugeIndex() {
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder.addMemory(33_000, undefined);

  builder.addFunction("main", kSig_i_i).exportFunc().addBody([
    ...wasmI32Const(0x80000000),
    kExprI32LoadMem16S, 65, 1, 0,
  ]);

  let instance;
  try {
    instance = builder.instantiate({});
  } catch (e) {
    assertContains('Out of memory', e.message);
    return;
  }
  assertEquals(0, instance.exports.main(0));
  %WasmTierUpFunction(instance.exports.main);
  assertEquals(0, instance.exports.main(0));
})();

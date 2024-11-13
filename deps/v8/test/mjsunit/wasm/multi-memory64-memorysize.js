// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-multi-memory --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function testMemorySizeMultimemory64Index0() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  const mem64 = builder.addMemory64(1, 1);
  const mem32 = builder.addMemory(4, 4);

  builder.addFunction('memorySize32Plus1', kSig_i_v)
    .addBody([kExprMemorySize, mem32, kExprI32Const, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction('memorySize64Plus1', kSig_l_v)
    .addBody([kExprMemorySize, mem64, kExprI64Const, 1, kExprI64Add])
    .exportFunc();

  let {memorySize32Plus1, memorySize64Plus1} = builder.instantiate().exports;
  assertEquals(2n, memorySize64Plus1());
  assertEquals(5, memorySize32Plus1());
})();

(function testMemorySizeMultimemory64Index1() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  const mem32 = builder.addMemory(4, 4);
  const mem64 = builder.addMemory64(1, 1);

  builder.addFunction('memorySize32Plus1', kSig_i_v)
    .addBody([kExprMemorySize, mem32, kExprI32Const, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction('memorySize64Plus1', kSig_l_v)
    .addBody([kExprMemorySize, mem64, kExprI64Const, 1, kExprI64Add])
    .exportFunc();

  let {memorySize32Plus1, memorySize64Plus1} = builder.instantiate().exports;
  assertEquals(2n, memorySize64Plus1());
  assertEquals(5, memorySize32Plus1());
})();

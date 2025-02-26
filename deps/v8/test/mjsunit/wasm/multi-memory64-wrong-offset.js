// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function testLoadOOBOffset() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  builder.addMemory64(1, 1);
  let mem32_idx = builder.addMemory(1, 1);

  builder.addFunction('load32', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprI32LoadMem, 0x40, mem32_idx, ...wasmSignedLeb64(0x1n << 33n)
      ]).exportFunc();

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError);
})();

(function testLoadInBoundsOffset() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();

  builder.addMemory(1, 1);
  let mem64_idx = builder.addMemory64(1, 1);

  builder.addFunction('load64', kSig_l_l)
      .addBody([
        kExprLocalGet, 0,
        kExprI64LoadMem, 0x40, mem64_idx, ...wasmSignedLeb64(0x1n << 33n)
      ]).exportFunc();

  builder.instantiate();
})();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-wasm-memory-moving

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const non_growable_memory = new WebAssembly.Memory({initial: 1, maximum: 1})

const builder = new WasmModuleBuilder();
builder.addType(kSig_v_v);
builder.addImport('imp', 'thrower', kSig_v_v);
builder.addImportedMemory('imp', 'mem0', 1, 1);
builder.addImportedMemory('imp', 'mem1', 1, 65536);
builder.addFunction('bad', kSig_i_v)
  .addBody([
    kExprI32Const, 0x0,
    kExprI32Const, 0x2b,
    kExprI32StoreMem, 0x40, 0x01, 0x00,
    kExprTry, kWasmVoid,
      kExprCallFunction, 0x00,
    kExprCatchAll,
      kExprI32Const, 0x0,
      kExprI32Const, 0x2a,
      kExprI32StoreMem, 0x40, 0x01, 0x00,
      kExprEnd,
    kExprI32Const, 0x0,
    kExprI32LoadMem, 0x40, 0x01, 0x00,
]).exportFunc();

const module = builder.toModule();

var max_pages = 10;

const growable_memory =
    new WebAssembly.Memory({initial: 1, maximum: max_pages});
function thrower() {
  growable_memory.grow(max_pages - 1);
  throw 'bleh';
}

const imports = {
  imp: {thrower: thrower, mem0: non_growable_memory, mem1: growable_memory}
};
const instance = new WebAssembly.Instance(module, imports);

instance.exports.bad()

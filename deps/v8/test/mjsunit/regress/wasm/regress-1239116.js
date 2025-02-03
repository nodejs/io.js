// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.exportMemoryAs('memory');
builder.addFunction('main', kSig_i_v)
    .addBody([
      kExprI32Const, 0,         // i32.const
      kExprI32LoadMem8S, 0, 0,  // i32.load8_s
      kExprI32LoadMem, 0, 0,    // i32.load
    ])
    .exportFunc();
const instance = builder.instantiate();
let mem = new Uint8Array(instance.exports.memory.buffer);
mem[0] = -1;
assertTraps(kTrapMemOutOfBounds, instance.exports.main);

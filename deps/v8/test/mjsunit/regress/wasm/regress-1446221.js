// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory();
let sig = makeSig([], new Array(16).fill(kWasmI32))
builder.addFunction('main', sig)
    .addBody([
      kExprUnreachable,
      kExprBrOnNull, 0,
      kExprDrop
    ])
    .exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapUnreachable, instance.exports.main);

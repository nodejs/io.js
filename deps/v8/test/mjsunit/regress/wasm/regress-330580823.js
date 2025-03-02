// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let mem = builder.addMemory(16, 32);
builder.addFunction(undefined, kSig_v_v)
    .addBody([
      ...wasmI32Const(-61),
      kExprI32Const, 1,
      ...GCInstr(kExprStringNewUtf8Try), mem,
      kExprBr, 0,
    ]).exportAs('callStringNewUtf8Try');

builder.addFunction(undefined, kSig_v_v)
    .addBody([
      ...wasmI32Const(-61),
      kExprI32Const, 1,
      ...GCInstr(kExprStringNewWtf16), mem,
      kExprBr, 0,
    ]).exportAs('callStringNewWtf16');

let kBuiltins = {builtins: ['js-string', 'text-decoder', 'text-encoder']};
const instance = builder.instantiate({}, kBuiltins);

assertTraps(kTrapMemOutOfBounds, () => instance.exports.callStringNewUtf8Try());
assertTraps(kTrapMemOutOfBounds, () => instance.exports.callStringNewWtf16());

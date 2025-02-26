// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-inlining --no-liftoff --no-wasm-lazy-compilation

// When inlining a tail call with multi-return inside a call with exceptions,
// we need to set the control input of the projections to the IfSuccess node.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

let tag = builder.addTag(kSig_v_v);

let callee = builder.addFunction("callee", kSig_ii_i)
  .addBody([kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
            kExprCallFunction, 2,
            kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub,
            kExprCallFunction, 2]);

let tail_call = builder.addFunction("tail_call", kSig_ii_i)
  .addBody([kExprLocalGet, 0, kExprReturnCall, callee.index]);

builder.addFunction("caller", kSig_i_i)
  .addBody([kExprTry, kWasmI32,
              kExprLocalGet, 0, kExprCallFunction, tail_call.index, kExprI32Mul,
            kExprCatch, tag,
              kExprLocalGet, 0,
            kExprEnd])

builder.instantiate();

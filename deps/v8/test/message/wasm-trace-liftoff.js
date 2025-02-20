// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm --no-wasm-tier-up --liftoff
// Flags: --no-wasm-inlining-call-indirect

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let kRet23Function = builder.addFunction('ret_23', kSig_i_v)
                         .addBody([kExprI32Const, 23])
                         .exportFunc()
                         .index;
let kCall23Function = builder.addFunction('call_23', kSig_i_v)
                          .addBody([kExprCallFunction, kRet23Function])
                          .exportFunc()
                          .index;
let kRet57Function = builder.addFunction('ret_57', kSig_l_v)
                         .addBody([kExprI64Const, 57])
                         .exportFunc()
                         .index;
let kUnnamedFunction = builder.addFunction(undefined, kSig_l_v)
                           .addBody([kExprCallFunction, kRet57Function])
                           .index;
let kRet0Function = builder.addFunction('ret_0', kSig_f_v)
                        .addBody(wasmF32Const(0))
                        .exportFunc()
                        .index;
let kRet1Function = builder.addFunction('ret_1', kSig_d_v)
                        .addBody(wasmF64Const(1))
                        .exportFunc()
                        .index;
let kIdentityFunction = builder.addFunction('identity', kSig_i_i)
                            .addBody([kExprLocalGet, 0])
                            .exportFunc()
                            .index;
let kCallIdentityFunction = builder.addFunction('call_identity', kSig_i_v)
                                .addBody([
                                  kExprI32Const, 42,                    // -
                                  kExprCallFunction, kIdentityFunction  // -
                                ])
                                .exportFunc()
                                .index;
let kVoidFunction =
    builder.addFunction('void', kSig_v_v).addBody([]).exportFunc().index;
builder.addFunction('main', kSig_v_v)
    .addBody([
      kExprCallFunction, kVoidFunction,                    // -
      kExprCallFunction, kCall23Function, kExprDrop,       // -
      kExprCallFunction, kUnnamedFunction, kExprDrop,      // -
      kExprCallFunction, kRet0Function, kExprDrop,         // -
      kExprCallFunction, kRet1Function, kExprDrop,         // -
      kExprCallFunction, kCallIdentityFunction, kExprDrop  // -
    ])
    .exportAs('main');

let instance = builder.instantiate();
instance.exports.main();

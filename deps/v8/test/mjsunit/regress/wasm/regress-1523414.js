// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --experimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function BasicTest() {
  const builder = new WasmModuleBuilder();
  builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
  builder.addType(makeSig([], []));
  builder.addTag(makeSig([], []));
  // Generate function 1 (out of 2).
  builder.addFunction(undefined, 0 /* sig */)
    .addBodyWithEnd([
  // signature: i_iii
  // body:
  kExprTry, 0x40,  // try @1
    kExprCallFunction, 0x01,  // call function #1: v_v
  kExprCatch, 0x00,  // catch @72
    kExprEnd,  // end @74
  kExprI32Const, 2,
  kExprEnd,  // end @78
  ]);
  // Generate function 2 (out of 2).
  builder.addFunction(undefined, 1 /* sig */)
    .addBodyWithEnd([
  // signature: v_v
  // body:
  kExprTry, 0x40,  // try @1
    kExprThrow, 0x00,  // throw
  kExprDelegate, 0x00,  // delegate
  kExprEnd,  // end @7
  ]);
  builder.addExport('main', 0);
  const instance = builder.instantiate();

  assertEquals(2, instance.exports.main(1, 2, 3));
})();

(function TestWithLoop() {
  const builder = new WasmModuleBuilder();
  builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
  builder.addType(makeSig([], []));
  builder.addTag(makeSig([], []));
  // Generate function 1 (out of 2).
  builder.addFunction(undefined, 0 /* sig */)
    .addBodyWithEnd([
  // signature: i_iii
  // body:
  kExprTry, 0x40,  // try @1
    kExprCallFunction, 0x01,  // call function #1: v_v
  kExprCatch, 0x00,  // catch @72
    kExprEnd,  // end @74
  kExprI32Const, 2,
  kExprEnd,  // end @78
  ]);
  // Generate function 2 (out of 2).
  builder.addFunction(undefined, 1 /* sig */)
    .addLocals(kWasmI32, 1)
    .addBodyWithEnd([
  // signature: v_v
  // body:
  kExprI32Const, 10, kExprLocalSet, 0,
  kExprLoop, kWasmVoid,
    kExprLocalGet, 0,
    kExprIf, kWasmVoid,
      kExprTry, 0x40,  // try @1
        kExprThrow, 0x00,  // throw
      kExprDelegate, 0x00,  // delegate
    kExprEnd,
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalTee, 0,
    kExprBrIf, 0,
    kExprEnd,  // end @7
  kExprEnd
  ]);
  builder.addExport('main', 0);
  const instance = builder.instantiate();

  assertEquals(2, instance.exports.main(1, 2, 3));
})();

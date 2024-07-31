// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function TestStackTrace(testFct, trap, expected) {
  assertTraps(trap, testFct);
  try {
    testFct();
    assertUnreachable();
  } catch(e) {
    let regex = /at [^ ]+ \(wasm[^\[]+\[[0-9+]\]:(0x[0-9a-f]+)\)/;
    let match = e.stack.match(regex);
    assertEquals(expected, match[1])
  }
}

(function CodeLocationRefCast() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let otherStruct = builder.addStruct([makeField(kWasmI64, true)]);

  builder.addFunction('createOther', makeSig([kWasmI64], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, otherStruct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('main', makeSig([kWasmExternRef], []))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, kStructRefCode,  // abstract cast
      kGCPrefix, kExprRefCastNull, struct,          // type cast
      kGCPrefix, kExprRefCast, struct,              // null check
      kExprDrop,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  let other = wasm.createOther(5n);

  TestStackTrace(() => wasm.main(3), kTrapIllegalCast, '0x50');
  TestStackTrace(() => wasm.main(other), kTrapIllegalCast, '0x53');
  TestStackTrace(() => wasm.main(null), kTrapIllegalCast, '0x56');
})();

(function CodeLocationArrayLen() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, kArrayRefCode,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  TestStackTrace(() => wasm.arrayLen(null), kTrapNullDereference, '0x2e');
})();

(function CodeLocationStructGet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('structGet', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  TestStackTrace(() => wasm.structGet(null), kTrapNullDereference, '0x35');
})();

(function CodeLocationRefCastUnrelated() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let structA = builder.addStruct([makeField(kWasmI64, true)]);
  let structB = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStructA', makeSig([kWasmI64], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, structA,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('structGet', makeSig([kWasmExternRef], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, structA,
      // Convert static type to anyref.
      kExprLocalSet, 1, kExprLocalGet, 1,
      // The following cast can be optimized into "trap if not null".
      kGCPrefix, kExprRefCastNull, structB,
      // The struct.get is only reachable for null; it will always trap with
      // kNullDereference if reached.
      kGCPrefix, kExprStructGet, structB, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  TestStackTrace(() => wasm.structGet(null), kTrapNullDereference, '0x64');
  let a = wasm.createStructA(123n);
  TestStackTrace(() => wasm.structGet(a), kTrapIllegalCast, '0x61');
  assertTraps(kTrapIllegalCast, () => wasm.structGet(a));
})();

(function CodeLocationRefCastSucceedsIfNotNull() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('structGet', makeSig([kWasmExternRef], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, struct,
      // Convert static type to anyref.
      kExprLocalSet, 1, kExprLocalGet, 1,
      // The following cast can be optimized into "trap if null".
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmStruct = wasm.createStruct(123);
  assertEquals(123, wasm.structGet(wasmStruct));
  TestStackTrace(() => wasm.structGet(null), kTrapIllegalCast, '0x5a');
})();

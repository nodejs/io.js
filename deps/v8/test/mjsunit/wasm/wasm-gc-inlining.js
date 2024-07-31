// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
  assertOptimized(fctToOptimize);
}

(function TestInliningStructGet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createStructNull', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getFieldNull', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  builder.addFunction('createStruct',
                      makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getField',
                      makeSig([wasmRefType(kWasmExternRef)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  // TODO(mliedtke): Consider splitting this loop as the reuse seems to prevent
  // proper feedback for the second iteration.
  for (let [create, get] of [
      [wasm.createStruct, wasm.getField],
      [wasm.createStructNull, wasm.getFieldNull]]) {
    let fct = () => {
      for (let i = 1; i <= 10; ++i) {
        const struct = create(i);
        assertEquals(i, get(struct));
      }
    };
    testOptimized(fct);

    // While these cases will all trap on the ref.cast, they cover very
    // different code paths in any.convert_extern.
    print("Test exceptional cases");
    const trap = kTrapIllegalCast;
    print("- test get null");
    const getNull = () => get(null);
    // If the null check is done by the wrapper, it throws a TypeError.
    // Otherwise it's a RuntimeError for the wasm trap.
    testOptimized(() => assertThrows(getNull), getNull);
    print("- test undefined");
    const getUndefined = () => get(undefined);
    testOptimized(() => assertTraps(trap, getUndefined), getUndefined);
    print("- test Smi");
    const getSmi = () => get(1);
    testOptimized(() => assertTraps(trap, getSmi), getSmi);
    print("- test -0");
    const getNZero = () => get(-0);
    testOptimized(() => assertTraps(trap, getNZero), getNZero);
    print("- test HeapNumber with fractional digits");
    const getFractional = () => get(0.5);
    testOptimized(() => assertTraps(trap, getFractional), getFractional);
    print("- test Smi/HeapNumber too large for i31ref");
    const getLargeNumber = () => get(0x4000_000);
    testOptimized(() => assertTraps(trap, getLargeNumber), getLargeNumber);

    print("- test inlining into try block");
    // TODO(14034): This is currently not supported by inlining yet.
    const getTry = () => {
      try {
        get(null);
      } catch (e) {
        assertTrue(e instanceof WebAssembly.RuntimeError ||
                   e instanceof TypeError);
        return;
      }
      assertUnreachable();
    };
    testOptimized(getTry);
  }
})();

(function TestInliningStructgetFieldTypes() {
  print(arguments.callee.name);
  const i64Value = Number.MAX_SAFE_INTEGER;
  const f64Value = 11.1;
  const i8Value = 123;
  const i16Value = 456;
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([
    makeField(kWasmI64, true),
    makeField(kWasmF64, true),
    makeField(kWasmI8, true),
    makeField(kWasmI16, true),
  ]);

  builder.addFunction('createStruct', makeSig([], [kWasmExternRef]))
    .addBody([
      ...wasmI64Const(i64Value),
      ...wasmF64Const(f64Value),
      ...wasmI32Const(i8Value),
      ...wasmI32Const(i16Value),
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getI64', makeSig([kWasmExternRef], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();
  builder.addFunction('getF64', makeSig([kWasmExternRef], [kWasmF64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 1,
    ])
    .exportFunc();
  builder.addFunction('getI8', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGetS, struct, 2,
    ])
    .exportFunc();
  builder.addFunction('getI16', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGetU, struct, 3,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let structVal = wasm.createStruct();
  print("- getI64");
  let getI64 =
    () => assertEquals(BigInt(i64Value), wasm.getI64(structVal));
  testOptimized(getI64);
  print("- getF64");
  let getF64 = () => assertEquals(f64Value, wasm.getF64(structVal));
  testOptimized(getF64);
  print("- getI8");
  let getI8 = () => assertEquals(i8Value, wasm.getI8(structVal));
  testOptimized(getI8);
  print("- getI16");
  let getI16 = () => assertEquals(i16Value, wasm.getI16(structVal));
  testOptimized(getI16);
})();

(function TestInliningMultiModule() {
  print(arguments.callee.name);

  let createModule = (fieldType) => {
    let builder = new WasmModuleBuilder();
    let struct = builder.addStruct([makeField(fieldType, true)]);

    builder.addFunction('createStruct', makeSig([fieldType], [kWasmExternRef]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprStructNew, struct,
        kGCPrefix, kExprExternConvertAny,
      ])
      .exportFunc();

    builder.addFunction('get', makeSig([kWasmExternRef], [fieldType]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprAnyConvertExtern,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0])
      .exportFunc();

    let instance = builder.instantiate({});
    return instance.exports;
  };

  let moduleA = createModule(kWasmI32);
  let moduleB = createModule(kWasmF64);
  let structA = moduleA.createStruct(123);
  let structB = moduleB.createStruct(321);

  // Only one of the two calls can be fully inlined. For the other call only the
  // wrapper is inlined.
  let multiModule =
    () => assertEquals(444, moduleA.get(structA) + moduleB.get(structB));
  testOptimized(multiModule);

  // The struct types are incompatible (but both use type index 0).
  // One of the two calls gets inlined and the inlined and the non-inlined
  // function have to keep apart the different wasm modules.
  let i = 0;
  let multiModuleTrap =
    () => ++i % 2 == 0 ? moduleA.get(structB) : moduleB.get(structA);
  testOptimized(() => assertTraps(kTrapIllegalCast, () => multiModuleTrap()),
                multiModuleTrap);
})();

(function TestInliningExternConvertAny() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([
    makeField(wasmRefNullType(0), true),
    makeField(kWasmI32, true),
  ]);

  builder.addFunction('createStruct',
      makeSig([kWasmExternRef, kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, struct,
      kExprLocalGet, 1,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getRef', makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();
  builder.addFunction('getVal', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 1,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let structA = wasm.createStruct(null, 1);
  let structB = wasm.createStruct(structA, 2);
  let getRef = () => assertSame(structA, wasm.getRef(structB));
  testOptimized(getRef);
  let getRefGetVal = () => assertSame(1, wasm.getVal(wasm.getRef(structB)));
  testOptimized(getRefGetVal);
})();

(function TestArrayLen() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, array,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let testLen = (expected, array) => assertSame(expected, wasm.arrayLen(array));
  let array0 = wasm.createArray(0);
  let array42 = wasm.createArray(42);
  testOptimized(() => testLen(0, array0), testLen);
  testOptimized(() => testLen(42, array42), testLen);
  testOptimized(
    () => assertTraps(kTrapNullDereference, () => testLen(-1, null)), testLen);
})();

(function TestArrayGet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('createArray',
      makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kGCPrefix, kExprArrayNewFixed, array, 3,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, array,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmArray = wasm.createArray(10, -1, 1234567);
  let get =
    (expected, array, index) =>
      assertEquals(expected, wasm.get(array, index));
  testOptimized(() => get(10, wasmArray, 0), get);
  testOptimized(() => get(-1, wasmArray, 1), get);
  testOptimized(() => get(1234567, wasmArray, 2), get);
  testOptimized(
    () => assertTraps(kTrapArrayOutOfBounds, () => get(-1, wasmArray, -1)),
    get);
  testOptimized(
    () => assertTraps(kTrapArrayOutOfBounds, () => get(-1, wasmArray, 3)), get);
  testOptimized(
    () => assertTraps(kTrapNullDereference, () => get(-1, null)), get);
})();

(function TestArrayGetPacked() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI8, true);

  builder.addFunction('createArray',
      makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kGCPrefix, kExprArrayNewFixed, array, 3,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getS', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGetS, array,
    ])
    .exportFunc();

  builder.addFunction('getU', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGetU, array,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmArray = wasm.createArray(10, -1, -123);
  {
    print("- test getS");
    let getS =
      (expected, array, index) =>
        assertEquals(expected, wasm.getS(array, index));
    testOptimized(() => getS(10, wasmArray, 0), getS);
    testOptimized(() => getS(-1, wasmArray, 1), getS);
    testOptimized(() => getS(-123, wasmArray, 2), getS);
    testOptimized(
      () => assertTraps(kTrapArrayOutOfBounds, () => getS(-1, wasmArray, -1)),
      getS);
    testOptimized(
      () => assertTraps(kTrapArrayOutOfBounds, () => getS(-1, wasmArray, 3)),
      getS);
    testOptimized(
      () => assertTraps(kTrapNullDereference, () => getS(-1, null)), getS);
  }
  {
    print("- test getU");
    let getU =
      (expected, array, index) =>
        assertEquals(expected, wasm.getU(array, index));
    testOptimized(() => getU(10, wasmArray, 0), getU);
    testOptimized(() => getU(255, wasmArray, 1), getU);
    testOptimized(() => getU(133, wasmArray, 2), getU);
    testOptimized(
      () => assertTraps(kTrapArrayOutOfBounds, () => getU(-1, wasmArray, -1)),
      getU);
    testOptimized(
      () => assertTraps(kTrapArrayOutOfBounds, () => getU(-1, wasmArray, 3)),
      getU);
    testOptimized(
      () => assertTraps(kTrapNullDereference, () => getU(-1, null)), getU);
  }
})();

(function TestCastArray() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);
  let struct = builder.addStruct([makeField(kWasmI32, true)]);

  builder.addFunction('createArray', makeSig([], [kWasmExternRef]))
    .addBody([
      kGCPrefix, kExprArrayNewFixed, array, 0,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();
  builder.addFunction('createStruct', makeSig([], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 42,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('castArray', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      // Generic cast to ref.array.
      kGCPrefix, kExprRefCast, kArrayRefCode,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmArray = wasm.createArray();
  let wasmStruct = wasm.createStruct();
  let castArray = (value) => wasm.castArray(value);
  let trap = kTrapIllegalCast;
  testOptimized(() => assertTraps(trap, () => castArray(null), castArray));
  testOptimized(() => assertTraps(trap, () => castArray(1), castArray));
  testOptimized(
    () => assertTraps(trap, () => castArray(wasmStruct), castArray));
  testOptimized(() => assertEquals(0, castArray(wasmArray)), castArray);
})();

(function TestInliningArraySet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI64, true);

  builder.addFunction('createArray',
      makeSig([kWasmI64, kWasmI64, kWasmI64], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kGCPrefix, kExprArrayNewFixed, array, 3,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, array,
    ])
    .exportFunc();

  builder.addFunction('set', makeSig([kWasmExternRef, kWasmI32, kWasmI64], []))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kGCPrefix, kExprArraySet, array,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmArray = wasm.createArray(0n, 1n, 2n);
  let writeAndRead = (array, index, value) => {
    wasm.set(array, index, value);
    assertEquals(value, wasm.get(array, index));
  };
  testOptimized(() => writeAndRead(wasmArray, 0, 123n), writeAndRead);
  testOptimized(() => writeAndRead(wasmArray, 1, -123n), writeAndRead);
  testOptimized(() => writeAndRead(wasmArray, 2, 0n), writeAndRead);
  testOptimized(
    () => assertTraps(kTrapArrayOutOfBounds,
                      () => writeAndRead(wasmArray, 3, 0n)),
    writeAndRead);
  testOptimized(
    () => assertTraps(kTrapArrayOutOfBounds,
                      () => writeAndRead(wasmArray, -1, 0n),
    writeAndRead));
  testOptimized(
    () => assertTraps(kTrapNullDereference, () => writeAndRead(null, 0, 0n),
    writeAndRead));
})();

(function TestInliningStructSet() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI64, true)]);

  builder.addFunction('createStruct',
      makeSig([kWasmI64], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef], [kWasmI64]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, struct,
      kGCPrefix, kExprStructGet, struct, 0,
    ])
    .exportFunc();

  builder.addFunction('set', makeSig([kWasmExternRef, kWasmI64], []))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, struct,
      kExprLocalGet, 1,
      kGCPrefix, kExprStructSet, struct, 0,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let wasmStruct = wasm.createStruct(0n);
  let writeAndRead = (struct, value) => {
    wasm.set(struct, value);
    assertEquals(value, wasm.get(struct));
  };
  testOptimized(() => writeAndRead(wasmStruct, 123n), writeAndRead);
  testOptimized(() => writeAndRead(wasmStruct, -123n), writeAndRead);
  testOptimized(() => writeAndRead(wasmStruct, 0n), writeAndRead);
  testOptimized(
    () => assertTraps(kTrapNullDereference, () => writeAndRead(null, 0n),
    writeAndRead));
})();

function testStackTrace(error, expected) {
  try {
    let stack = error.stack.split("\n");
    assertTrue(stack.length >= expected.length);
    for (let i = 0; i < expected.length; ++i) {
      assertMatches(expected[i], stack[i]);
    }
  } catch(failure) {
    print("Actual stack trace: ", error.stack);
    throw failure;
  }
}

(function TestInliningTrapStackTrace() {
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

  builder.addFunction('getField', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  { // Test simple case.
    const getTrap = () => {
      return wasm.getField(null);
    };

    const testTrap = () => {
      try {
        getTrap();
        assertUnreachable();
      } catch(e) {
        testStackTrace(e, [
          /RuntimeError: illegal cast/,
          /at getField \(wasm:\/\/wasm\/[0-9a-f]+:wasm-function\[1\]:0x50/,
          /at getTrap .*\.js:639:19/,
        ]);
      }
    };
    testOptimized(testTrap, getTrap);
  }

  { // Test wasm inlined into JS inlined into JS.
    const getTrapNested = (obj) => {
      %PrepareFunctionForOptimization(inlined);
      return inlined();
      function inlined() { return wasm.getField(obj); };
    };

    let wasmStruct = wasm.createStruct(42);
    // Warmup without exception. This seems to help inlining the JS function.
    testOptimized(() => assertEquals(42, getTrapNested(wasmStruct)),
                  getTrapNested);
    try {
      getTrapNested(null);
      assertUnreachable();
    } catch(e) {
      testStackTrace(e, [
        /RuntimeError: illegal cast/,
        /at getField \(wasm:\/\/wasm\/[0-9a-f]+:wasm-function\[1\]:0x50/,
        /at inlined .*\.js:661:40/,
        /at getTrapNested .*\.js:660:14/,
      ]);
      assertOptimized(getTrapNested);
    }
  }
})();

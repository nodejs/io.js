// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
  // Re-prepare the function immediately to make sure type feedback isn't
  // cleared by an untimely gc.
  %PrepareFunctionForOptimization(fctToOptimize);
  assertOptimized(fctToOptimize);
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

  builder.addFunction('getElement', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, struct,
      kGCPrefix, kExprStructGet, struct, 0])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;


  const getTrap = () => {
    return wasm.getElement(null);
  };

  Error.prepareStackTrace = (e, frames) => {
    let frame = frames[0];
    assertSame(instance, frame.getThis());
    assertSame(null, frame.getTypeName());
    assertSame(1, frame.getFunction());
    assertEquals('getElement', frame.getFunctionName());
    assertSame(null, frame.getMethodName());
    assertMatches(/wasm:\/\/wasm\/[0-9a-f]+/,
                 frame.getFileName());
    assertEquals(0x53, frame.getColumnNumber());
    assertSame(undefined, frame.getEvalOrigin());
    assertFalse(frame.isToplevel());
    assertFalse(frame.isEval());
    assertFalse(frame.isNative());
    assertFalse(frame.isConstructor());
    assertFalse(frame.isAsync());
    assertFalse(frame.isPromiseAll());
    assertSame(null, frame.getPromiseIndex());

    // Inspect the outer JS frame.
    frame = frames[1];
    assertSame(this, frame.getThis());
    assertSame(null, frame.getTypeName());
    assertSame(getTrap, frame.getFunction());
    assertEquals(getTrap.name, frame.getFunctionName());
    assertSame(null, frame.getMethodName());
    assertMatches(/.*wasm-gc-inlining-stacktrace-api\.js/,
                 frame.getFileName());
    assertEquals(17, frame.getColumnNumber());
    assertSame(undefined, frame.getEvalOrigin());
    assertTrue(frame.isToplevel());
    assertFalse(frame.isEval());
    assertFalse(frame.isNative());
    assertFalse(frame.isConstructor());
    assertFalse(frame.isAsync());
    assertFalse(frame.isPromiseAll());
    assertSame(null, frame.getPromiseIndex());

    // Return 'custom' stack trace.
    return 'This is a stack trace';
  };

  const testTrap = () => {
    try {
      getTrap();
      assertUnreachable();
    } catch(e) {
      assertEquals('This is a stack trace', e.stack);
    }
  };
  testOptimized(testTrap, getTrap);

  {
    print('Test with wasm call within a try catch block');

    // Note: At the moment of writing this test case, inlining of the wasm call
    // is not yet supported. This test only ensures that supporting it in the
    // future won't break the stack trace API behavior.
    const getTrapTryCatch = () => {
      try {
        return wasm.getElement(null);
      } catch (e) {
        assertEquals('This is a stack trace', e.stack);
      }
    };

    Error.prepareStackTrace = (e, frames) => {
      let frame = frames[0];
      assertSame(instance, frame.getThis());
      assertSame(null, frame.getTypeName());
      assertSame(1, frame.getFunction());
      assertEquals('getElement', frame.getFunctionName());
      assertSame(null, frame.getMethodName());
      assertMatches(/wasm:\/\/wasm\/[0-9a-f]+/,
                   frame.getFileName());
      assertEquals(0x53, frame.getColumnNumber());
      assertSame(undefined, frame.getEvalOrigin());
      assertFalse(frame.isToplevel());
      assertFalse(frame.isEval());
      assertFalse(frame.isNative());
      assertFalse(frame.isConstructor());
      assertFalse(frame.isAsync());
      assertFalse(frame.isPromiseAll());
      assertSame(null, frame.getPromiseIndex());

      // Inspect the outer JS frame.
      frame = frames[1];
      assertSame(this, frame.getThis());
      assertSame(null, frame.getTypeName());
      assertSame(getTrapTryCatch, frame.getFunction());
      assertEquals(getTrapTryCatch.name, frame.getFunctionName());
      assertSame(null, frame.getMethodName());
      assertMatches(/.*wasm-gc-inlining-stacktrace-api\.js/,
                   frame.getFileName());
      assertEquals(21, frame.getColumnNumber());
      assertSame(undefined, frame.getEvalOrigin());
      assertTrue(frame.isToplevel());
      assertFalse(frame.isEval());
      assertFalse(frame.isNative());
      assertFalse(frame.isConstructor());
      assertFalse(frame.isAsync());
      assertFalse(frame.isPromiseAll());
      assertSame(null, frame.getPromiseIndex());

      // Return 'custom' stack trace.
      return 'This is a stack trace';
    };

    testOptimized(getTrapTryCatch);
  }
})();

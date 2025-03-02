// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax
// Flags: --turbofan --turboshaft
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0
// Flags: --no-liftoff --wasm-fast-api
// --wasm-lazy-compilation is needed so that wellknown imports work in the
// absence of dynamic tiering.
// Flags: --wasm-lazy-compilation
// Flags: --fast-api-allow-float-in-sim

load('test/mjsunit/wasm/wasm-module-builder.js');

const fast_c_api = new d8.test.FastCAPI();

(function TestCallCatches() {
  print(arguments.callee.name);
  let numThrow = 0;
  let numNoThrow = 0;

  function callThrowingFunction() {
      fast_c_api.throw_no_fallback();
  }

  %PrepareFunctionForOptimization(callThrowingFunction);
  try {
    callThrowingFunction();
    numNoThrow++;
  } catch (e) {
    numThrow++;
  }
  %OptimizeFunctionOnNextCall(callThrowingFunction);
  fast_c_api.reset_counts();
  try {
    callThrowingFunction();
    numNoThrow++;
  } catch (e) {
    numThrow++;
  }

  assertOptimized(callThrowingFunction);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
  assertEquals(2, numThrow);
})();

(function TestFunctionCatches() {
  print(arguments.callee.name);
  let numThrow = 0;

  function checkThrow() {
    try {
      fast_c_api.throw_no_fallback();
    } catch (e) {
      numThrow++;
    }
  }

  %PrepareFunctionForOptimization(checkThrow);
  checkThrow();
  %OptimizeFunctionOnNextCall(checkThrow);
  fast_c_api.reset_counts();
  checkThrow();
  assertOptimized(checkThrow);

  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
  assertEquals(2, numThrow);
})();

(function TestCatchInWebAssembly() {
  print(arguments.callee.name);
  const fast_c_api = new d8.test.FastCAPI();
  // The counter is global, so even for a new object we have to do a reset.
  fast_c_api.reset_counts();

  let builder = new WasmModuleBuilder();
  let js_tag = builder.addImportedTag('mod', 'tag', kSig_v_r);

  let kJSThrowRef = builder.addImport('mod', 'throwRef', kSig_i_r);
  let kCheckStack = builder.addImport('mod', 'checkStack', kSig_v_r);
  try_sig_index = builder.addType(kSig_i_v);
  builder.addFunction('test', kSig_i_r)
    .addBody([
      kExprTry, try_sig_index,
      kExprLocalGet, 0,
      kExprCallFunction, kJSThrowRef,
      kExprCatch, js_tag,
      kExprCallFunction, kCheckStack,
      kExprI32Const, 27,
      kExprEnd,
    ])
    .exportFunc();

  let instance = builder.instantiate({
    mod: {
      throwRef: Function.prototype.call.bind(fast_c_api.throw_no_fallback),
      checkStack: e => {
        assertMatches(/Exception from fast callback/, e.stack);
        // regexp to test for a string like
        // "at test (wasm://wasm/28882bba:wasm-function[2]:0x66)"
        const regex =
         'at test \\(wasm\\://wasm/[0-9,a-f]+\\:wasm-function\\[2\\]\\:0x66\\)';
        assertMatches(new RegExp(regex), e.stack);
      },
      tag: WebAssembly.JSTag,
    }
  });
  // Catch with implicit wrapping.
  assertSame(27, instance.exports.test(fast_c_api));
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
})();

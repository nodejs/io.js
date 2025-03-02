// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function CheckStackTrace(thrower, reference, topmost_wasm_func) {
  let reference_exception;
  let actual_exception;
  try {
    thrower();
    assertUnreachable();
  } catch (e) {
    actual_exception = e;
  }
  try {
    reference();
    assertUnreachable();
  } catch (e) {
    reference_exception = e;
  }
  assertInstanceof(actual_exception, reference_exception.constructor);
  let actual_stack = actual_exception.stack.split('\n');
  let reference_stack = reference_exception.stack.split('\n');
  assertEquals(reference_stack[0], actual_stack[0]);
  assertEquals(reference_stack[1], actual_stack[1]);
  assertTrue(
      actual_stack[2].startsWith(`    at ${topmost_wasm_func} (wasm://wasm/`));
}

let builder = new WasmModuleBuilder();
let sig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);
let kRefString = wasmRefType(kWasmStringRef);
let toLowerCase = builder.addImport("m", "toLowerCase", sig_w_w);

builder.addFunction('call_tolower', sig_w_w).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprCallFunction, toLowerCase,
]);

let module = builder.toModule();

let recognizable = Function.prototype.call.bind(String.prototype.toLowerCase);
let recognizable_imports = { m: { toLowerCase: recognizable } };

let instance1 = new WebAssembly.Instance(module, recognizable_imports);
let call_tolower = instance1.exports.call_tolower;
assertEquals("abc", call_tolower("ABC"));
%WasmTierUpFunction(call_tolower);
assertEquals("abc", call_tolower("ABC"));

// Null should be handled correctly (by throwing the same TypeError that
// JavaScript would throw).
CheckStackTrace(
    () => call_tolower(null), () => String.prototype.toLowerCase.call(null),
    'call_tolower');

// Creating a second instance with identical imports should not cause
// recompilation.
console.log("Second instance.");
let instance2 = new WebAssembly.Instance(module, recognizable_imports);
assertEquals("def", instance2.exports.call_tolower("DEF"));

// Creating a third instance with different imports must not reuse the
// existing optimized code.
console.log("Third instance.");
let other_imports = { m: { toLowerCase: () => "foo" } };
let instance3 = new WebAssembly.Instance(module, other_imports);
assertEquals("foo", instance3.exports.call_tolower("GHI"));
assertEquals("def", instance2.exports.call_tolower("DEF"));
assertEquals("abc", instance1.exports.call_tolower("ABC"));

(function TestStringToLowercaseStringRef() {
  console.log('Testing String.toLowerCase with stringRef');
  let builder = new WasmModuleBuilder();
  let toLowerCaseNullable =
      builder.addImport('m', 'toLowerCaseNullable', sig_w_w);
  builder.addFunction('tolower_nullable', sig_w_w).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, toLowerCaseNullable,
  ]);
  let func = Function.prototype.call.bind(String.prototype.toLowerCase);
  toLowerCaseNullable = builder.instantiate({m: {toLowerCaseNullable: func}})
                            .exports.tolower_nullable;

  %WasmTierUpFunction(toLowerCaseNullable);
  assertEquals('abc', toLowerCaseNullable('ABC'));
  CheckStackTrace(
      () => toLowerCaseNullable(null),
      () => String.prototype.toLowerCase.call(null), 'tolower_nullable');
})();

(function TestStringToLowercaseRefString() {
  console.log('Testing String.toLowerCase with refString');
  let builder = new WasmModuleBuilder();
  let sig_refstr = makeSig([kRefString], [kRefString]);
  let toLowerCaseNonNullable =
      builder.addImport('m', 'toLowerCaseNonNullable', sig_refstr);
  builder.addFunction('tolower_non_nullable', sig_refstr).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, toLowerCaseNonNullable,
  ]);
  let func = Function.prototype.call.bind(String.prototype.toLowerCase);
  toLowerCaseNonNullable =
      builder.instantiate({m: {toLowerCaseNonNullable: func}})
          .exports.tolower_non_nullable;

  %WasmTierUpFunction(toLowerCaseNonNullable);
  assertEquals('abc', toLowerCaseNonNullable('ABC'));
})();

(function TestIntToString() {
  console.log("Testing IntToString");
  let builder = new WasmModuleBuilder();
  let sig_w_ii = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);
  let intToString = builder.addImport("m", "intToString", sig_w_ii);
  builder.addFunction('call_inttostring', sig_w_ii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, intToString,
  ]);
  let func = Function.prototype.call.bind(Number.prototype.toString);
  let instance = builder.instantiate({ m: { intToString: func } });
  let call_inttostring = instance.exports.call_inttostring;
  %WasmTierUpFunction(call_inttostring);
  assertEquals("42", call_inttostring(42, 10));
  assertEquals("-123", call_inttostring(-123, 10));
  assertEquals("2a", call_inttostring(42, 16));
  assertEquals("2147483647", call_inttostring(2147483647, 10));
  assertEquals("-2147483648", call_inttostring(-2147483648, 10));
  CheckStackTrace(
      () => call_inttostring(1, 99), () => func(1, 99), 'call_inttostring');
})();

(function TestDoubleToString() {
  console.log("Testing DoubleToString");
  let builder = new WasmModuleBuilder();
  let sig_d_w = makeSig([kWasmStringRef], [kWasmF64]);
  let sig_d_refstr = makeSig([kRefString], [kWasmF64]);
  let sig_w_d = makeSig([kWasmF64], [kWasmStringRef]);
  let doubleToString = builder.addImport('m', 'doubleToString', sig_w_d);
  let stringToDoubleNullable =
      builder.addImport('m', 'stringToDoubleNullable', sig_d_w);
  let stringToDoubleNonNullable =
      builder.addImport('m', 'stringToDoubleNonNullable', sig_d_refstr);
  builder.addFunction('call_doubletostring', sig_w_d).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, doubleToString,
  ]);
  builder.addFunction('call_stringtodouble_nullable', sig_d_w)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, stringToDoubleNullable,
      ]);
  builder.addFunction('call_stringtodouble_non_nullable', sig_d_refstr)
      .exportFunc()
      .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, stringToDoubleNonNullable,
      ]);
  let wasm = builder.instantiate({
    m: {
      doubleToString: Function.prototype.call.bind(Number.prototype.toString),
      stringToDoubleNullable: parseFloat,
      stringToDoubleNonNullable: parseFloat,
    }
  }).exports;
  let d2s = wasm.call_doubletostring;
  let s2d_nullable = wasm.call_stringtodouble_nullable;
  let s2d_non_nullable = wasm.call_stringtodouble_non_nullable;
  %WasmTierUpFunction(d2s);
  %WasmTierUpFunction(s2d_nullable);
  %WasmTierUpFunction(s2d_non_nullable);
  assertEquals("42", d2s(42));
  assertEquals("1234.5", d2s(1234.5));
  assertEquals("NaN", d2s(NaN));
  assertEquals(1234.5, s2d_nullable("1234.5"));
  assertEquals(1234.5, s2d_non_nullable("1234.5"));
  assertEquals(NaN, s2d_nullable(null));
})();

(function TestIndexOfWithStringRef() {
  console.log("Testing String.indexOf with stringRef");
  let builder = new WasmModuleBuilder();
  let sig_i_wwi =
      makeSig([kWasmStringRef, kWasmStringRef, kWasmI32], [kWasmI32]);
  let indexOf = builder.addImport("m", "indexOf", sig_i_wwi);
  builder.addFunction('call_indexof', sig_i_wwi).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, indexOf,
  ]);
  indexOf = builder.instantiate({
      m: {indexOf: Function.prototype.call.bind(String.prototype.indexOf)}
  }).exports.call_indexof;
  %WasmTierUpFunction(indexOf);
  assertEquals(2, indexOf("xxfooxx", "foo", 0));
  assertEquals(2, indexOf("xxfooxx", "foo", -2));
  assertEquals(-1, indexOf("xxfooxx", "foo", 100));
  // Make sure we don't lose bits when Smi-tagging of the start position.
  assertEquals(-1, indexOf("xxfooxx", "foo", 0x4000_0000));
  assertEquals(-1, indexOf("xxfooxx", "foo", 0x2000_0000));
  assertEquals(2, indexOf("xxfooxx", "foo", 0x8000_0000));  // Negative i32.
  // When first arg is null, throw; when second arg is null, convert.
  assertEquals(2, indexOf("xxnullxx", null, 0));
  CheckStackTrace(
      () => indexOf(null, 'null', 0),
      () => String.prototype.indexOf.call(null, 'null', 0), 'call_indexof');
})();

(function TestIndexOfRefString() {
  console.log("Testing String.indexOf with refString");
  let builder = new WasmModuleBuilder();
  let sig_refstr = makeSig([kRefString, kRefString, kWasmI32], [kWasmI32]);
  let indexOf = builder.addImport("m", "indexOf", sig_refstr);
  builder.addFunction('call_indexof', sig_refstr).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, indexOf,
  ]);
  indexOf = builder.instantiate({
      m: {indexOf: Function.prototype.call.bind(String.prototype.indexOf)}
  }).exports.call_indexof;
  %WasmTierUpFunction(indexOf);
  assertEquals(2, indexOf("xxfooxx", "foo", 0));
})();

(function TestToLocaleLower() {
  console.log("Testing String.toLocaleLowerCase");
  let builder = new WasmModuleBuilder();
  let sig_w_ww = makeSig([kWasmStringRef, kWasmStringRef], [kWasmStringRef]);
  let tolower = builder.addImport("m", "tolower", sig_w_ww);
  builder.addFunction('call_tolower', sig_w_ww).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, tolower,
  ]);
  tolower = builder.instantiate({
    m: {
      tolower: Function.prototype.call.bind(String.prototype.toLocaleLowerCase)
    }
  }).exports.call_tolower;
  %WasmTierUpFunction(tolower);
  assertEquals("abc", tolower("ABC", "en"));
  let has_i18n = typeof Intl !== "undefined";
  if (has_i18n) {
    // Check that the locale isn't ignored.
    assertEquals("\u0131", tolower("I", "az"));
    CheckStackTrace(
        () => tolower('ABC', null),
        () => String.prototype.toLocaleLowerCase.call('ABC', null),
        'call_tolower');
  } else {
    // Non-i18n builds ignore the locale parameter.
    assertEquals("i", tolower("I", "az"));
    assertEquals('abc', tolower('ABC', null));
    assertEquals('abc', String.prototype.toLocaleLowerCase.call('ABC', null));
  }
  CheckStackTrace(
      () => tolower(null, 'en'),
      () => String.prototype.toLocaleLowerCase.call(null, 'en'),
      'call_tolower');
})();

// Test that we discard background tier-up compilation results in case the
// assumptions were invalidated in the meantime.
// Regression test for https://crbug.com/382326704.
(function TestInvalidatingAssumptionsDuringBackgroundCompile() {
  console.log('Testing invalidating assumptions during background compilation');
  let builder = new WasmModuleBuilder();
  let sig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);
  let toLowerCase = builder.addImport('m', 'toLowerCase', sig_w_w);

  builder.addFunction('call_tolower', sig_w_w).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, toLowerCase,
  ]);

  // Add a global with a random value to ensure that this is a fresh module (not
  // from the NativeModule cache).
  builder.addGlobal(kWasmI32, true, false, wasmI32Const(2711));

  let module = builder.toModule();

  let recognizable = Function.prototype.call.bind(String.prototype.toLowerCase);
  let recognizable_imports = {m: {toLowerCase: recognizable}};

  let instance1 = new WebAssembly.Instance(module, recognizable_imports);
  let call_tolower1 = instance1.exports.call_tolower;
  // Note: We cannot assert that `call_tolower1` is Liftoff at this point; in
  // multi-isolate test execution, it could be tiered up already.
  assertEquals('abc', call_tolower1('ABC'));
  %WasmTriggerTierUpForTesting(call_tolower1);

  // Give background compilation a little time to start...
  for (let i = Math.floor(Math.random() * 10000); i > 0; --i);

  // While background compilation is (potentially) running, create another
  // instance which invalidates the assumptions. The background-compiled code
  // should then not be used.
  let other_imports = {m: {toLowerCase: () => 'foo'}};
  let instance2 = new WebAssembly.Instance(module, other_imports);
  let call_tolower2 = instance2.exports.call_tolower;
  assertEquals('foo', call_tolower2('GHI'));
  let last_print = performance.now();
  while (!%IsTurboFanFunction(call_tolower2)) {
    assertEquals('foo', call_tolower2('GHI'));
    if (performance.now() - last_print > 1000) {
      print('Still waiting for background compilation to finish...');
      last_print = performance.now();
      // Also, re-trigger in case the background compilation finished but the
      // result was discarded because of the invalidated assumptions.
      %WasmTriggerTierUpForTesting(call_tolower1);
    }
  }
  assertEquals('foo', call_tolower2('GHI'));
})();

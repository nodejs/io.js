// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const dummyFunc =
    new WebAssembly.Function({parameters: [], results: []}, () => 15);

const tests = [
  {type: 'i32', input: 12.5, expected: 12},
  {type: 'i64', input: 0xffff_ffff_ffff_ffff_ffffn, expected: -1n},
  {
    type: 'f32',
    input: Number.MAX_SAFE_INTEGER,
    // F32 cannot represent MAX_SAFE_INTEGER, so it rounds to the next closest
    // representable number.
    expected: Number.MAX_SAFE_INTEGER + 1
  },
  {type: 'f64', input: {valueOf: () => 15}, expected: 15},
  {type: 'externref', input: 'Foo', expected: 'Foo'},
  {type: 'anyfunc', input: null, expected: null},
  {type: 'anyfunc', input: dummyFunc, expected: dummyFunc},
];

(function TestNoReturn() {
  print(arguments.callee.name);
  const jsFunc =
      new WebAssembly.Function({parameters: [], results: []}, () => 15);

  assertEquals(undefined, jsFunc());
})();

(function TestSingleReturn() {
  print(arguments.callee.name);
  for (const test of tests) {
    const jsFunc = new WebAssembly.Function(
        {parameters: [], results: [test.type]}, () => test.input);

    assertEquals(test.expected, jsFunc());
  }
})();

(function TestSingleParam() {
  print(arguments.callee.name);
  for (const test of tests) {
    const jsFunc = new WebAssembly.Function(
        {parameters: [test.type], results: []},
        (param) => assertEquals(test.expected, param));
    jsFunc(test.input);
  }
})();

(function TestMultiParamMultiReturn() {
  print(arguments.callee.name);
  for (const param of tests) {
    for (const ret of tests) {
      const jsFunc = new WebAssembly.Function(
          {parameters: ['i32', param.type], results: ['f32', ret.type]},
          (foo, p) => {
            assertEquals(param.expected, p);
            return [22, ret.input];
          });
      const result = jsFunc(12, param.input);
      assertEquals(ret.expected, result[1]);
    }
  }
})();

(function TestAnyfuncThrows() {
  print(arguments.callee.name);

  let jsFunc = new WebAssembly.Function(
      {parameters: [], results: ['anyfunc']}, () => 'no function');

  assertThrows(() => jsFunc(), TypeError);
  jsFunc = new WebAssembly.Function(
      {parameters: ['anyfunc'], results: []}, () => 'no function');

  assertThrows(() => jsFunc('no function'), TypeError);

  jsFunc = new WebAssembly.Function(
      {parameters: [], results: ['i32', 'anyfunc']}, () => [12, 'no function']);

  assertThrows(() => jsFunc(), TypeError);
  jsFunc = new WebAssembly.Function(
      {parameters: ['f32', 'anyfunc'], results: []}, () => 'no function');

  assertThrows(() => jsFunc(32, 'no function'), TypeError);
})();

(function TestParamPops() {
  print(arguments.callee.name);
  function add(i, j) {
    return i + j;
  }
  const jsFunc =
      new WebAssembly.Function({parameters: ['f64'], results: ['i32']}, add);
  jsFunc();
  jsFunc(1, 2, 3, 4);
}) ();

(function TestInvalidParam() {
  print(arguments.callee.name);
    const jsFunc = new WebAssembly.Function(
        {parameters: ['v128'], results: []},
        (param) => assertUnreachable());
    assertThrows(jsFunc, TypeError);
})();

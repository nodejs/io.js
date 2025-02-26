// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-generic-wrapper --expose-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const debug = false;
// Use consecutive values as parameters for easier debugging.
const easyValues = false;

// This test tests the wasm-to-js wrapper with random signatures and random
// values. The test creates a WebAssembly function with the random signature.
// The WebAssembly function passes the incoming parameters directly to an
// imported JavaScript function. The JavaScript function checks that the
// incoming parameters match the original values passed to the WebAssembly
// function. The imported JavaScript function then returns random return values,
// which the WebAssembly function again forwards to the original caller. There
// the return values of the WebAssembly function are compared with the original
// random return values. `RunTest` is the core function, which runs the test
// case for a specific signature and value mapping. `RunTest` can be used well
// for debugging a specific signature. `GenerateAndRunTest` additionally
// generates the inputs for `RunTest`, which is good for fuzzing the js-to-wasm
// wrappers.

// Some documentation if the test fails in the CQ.
console.log("This test is a fuzzer, it tests the generic wasm-to-js wrapper");
console.log("with random signatures. If this test fails, then it may not fail");
console.log("for the CL that actually introduced the issue, but for a");
console.log("later CL in the CQ. You may want to use flako to identify that");
console.log("actual culprit.");

const kPossibleTypes = [kWasmI32, kWasmI64, kWasmF32, kWasmF64, kWasmExternRef];

function getRandomInt(max) {
  return Math.floor(Math.random() * max);
}

function TypeString(type) {
  switch (type) {
    case kWasmI32:
      return 'kWasmI32';
    case kWasmI64:
      return 'kWasmI64';
    case kWasmF32:
      return 'kWasmF32';
    case kWasmF64:
      return 'kWasmF64';
    case kWasmExternRef:
      return 'kWasmExternRef';
  }
}

// A set of interesting values that will be used as parameters.
let interestingParams = {};
interestingParams[kWasmI32] = [
  -1500000000, -0x4000_0000, -0x3fff_ffff, -1, 0, 1, 2, 0x3fff_ffff,
  0x4000_0000, 1500000000, 0x7fff_ffff, {valueOf: () => 15}, {
    toString: () => {
      gc();
      return '17';
    }
  }
];

interestingParams[kWasmI64] = [
  -0x7fff_ffff_ffff_ffffn, -0xffff_ffffn, -0x8000_0000n, -1500000000n,
  -0x4000_0000n, -0x3fff_ffffn, -1n, 0n, 1n, 2n, 0x3fff_ffffn, 0x4000_0000n,
  1500000000n, 0x7fff_ffffn, 0x8000_0000n, 0xffff_ffffn, 0x7fff_ffff_ffff_ffffn,
  {valueOf: () => 15n}, {
    toString: () => {
      gc();
      return '17';
    }
  }
];

interestingParams[kWasmF32] = [
  -Infinity,
  -(2 ** 65),
  -(2 ** 64),
  -(2 ** 63),
  -(2 ** 62),
  -(2 ** 29),
  -(2 ** 33),
  -(2 ** 32),
  -(2 ** 31),
  -(2 ** 30),
  -(2 ** 29),
  -1.5,
  -1,
  0,
  -0,
  1,
  1.5,
  2,
  2 ** 29,
  2 ** 30,
  2 ** 31,
  2 ** 32,
  2 ** 33,
  2 ** 62,
  2 ** 63,
  2 ** 64,
  2 ** 65,
  {valueOf: () => 16},
  {
    toString: () => {
      gc();
      return '18';
    }
  },
  Infinity,
  NaN
];

interestingParams[kWasmF64] = [
  -Infinity,
  -(2 ** 65),
  -(2 ** 64),
  -(2 ** 63),
  -(2 ** 62),
  -(2 ** 29),
  -(2 ** 33),
  -(2 ** 32),
  -(2 ** 31),
  -(2 ** 30),
  -(2 ** 29),
  -1.5,
  -1,
  0,
  -0,
  1,
  1.5,
  2,
  // 28 set bits, too many for float32.
  0xfffffff,
  2 ** 29,
  2 ** 30,
  2 ** 31,
  2 ** 32,
  2 ** 33,
  2 ** 62,
  2 ** 63,
  2 ** 64,
  2 ** 65,
  {valueOf: () => 14},
  {
    toString: () => {
      gc();
      return '19';
    }
  },
  Infinity,
  NaN
];

interestingParams[kWasmExternRef] = [
  17, 12.5, 17n, 0x7fff_ffff, {foo: 18}, [1, 2, 3], 'bar', () => 38, null,
  undefined
];

function GenerateValueArray(params) {
  let result = [];
  let nextValue = 1;
  for (let param of params) {
    if (easyValues) {
      switch (param) {
        case kWasmI32:
        case kWasmF32:
        case kWasmF64:
          result.push(nextValue++);
          break;
        case kWasmI64:
          result.push(BigInt(nextValue++));
          break;
        case kWasmExternRef:
          const val = nextValue++;
          result.push({ val: val });
      }
    } else {
      result.push(interestingParams[param][getRandomInt(
        interestingParams[param].length)]);
    }
  }
  return result;
}

function assertValueArray(original, transformed) {
  assertEquals(transformed.length, original.length);
  for (let i = 0; i < transformed.length; ++i) {
    const arg = transformed[i];
    if (typeof arg === 'bigint') {
      // For values of type I64.
      assertEquals(BigInt(original[i]), arg);
    } else if (typeof arg === 'number') {
      // For values of type I32, F32, and F64.
      assertEquals(Number(original[i]), arg);
    } else {
      // For values of type externref.
      assertEquals(original[i], arg);
    }
  }
}

function RunTest(params, returns) {
  if (debug) {
    let testcase = 'RunTest([';
    for (let i = 0; i < params.length; ++i) {
      testcase += (i == 0 ? '' : ', ') + TypeString(params[i]);
    }
    testcase += '], [';
    if (returns.length > 0) {
      testcase += TypeString(returns[0]);
      for (let i = 1; i < returns.length; ++i) {
        testcase += ', ' + TypeString(returns[i]);
      }
    }
    testcase += ']);';
    console.log(testcase);
  }

  const builder = new WasmModuleBuilder();
  const impSig = makeSig(params, returns);
  const impIndex = builder.addImport('m', 'f', impSig);
  const paramValues = GenerateValueArray(params);
  const returnValues = GenerateValueArray(returns);
  let body = [];
  for (let i = 0; i < params.length; ++i) {
    body.push(kExprLocalGet, i);
  }
  body.push(kExprCallFunction, impIndex);

  function impFunction() {
    assertValueArray(paramValues, arguments);
    if (returns.length === 0) return;
    if (returns.length === 1) return returnValues[0];
    return returnValues;
  };

  const sig = makeSig(params, returns);
  builder.addFunction('main', sig).addBody(body).exportFunc();
  const instance = builder.instantiate({ m: { f: impFunction } });
  let result = instance.exports.main(...paramValues);
  if (returns.length === 0) return;
  if (returns.length === 1) result = [result];
  assertValueArray(returnValues, result);
}

function GenerateAndRunTest() {
  // Generate signature.
  const kMaxParams = 20;
  const kMaxReturns = 10;
  const numParams = getRandomInt(kMaxParams) + 1;
  const numReturns = getRandomInt(kMaxReturns + 1);
  // The array of parameter types.
  const params = [];
  // The array of return types.
  const returns = [];
  for (let i = 0; i < numParams; ++i) {
    params.push(kPossibleTypes[getRandomInt(kPossibleTypes.length)]);
  }
  for (let i = 0; i < numReturns; ++i) {
    returns.push(kPossibleTypes[getRandomInt(kPossibleTypes.length)]);
  }
  RunTest(params, returns);
}
// Regression tests:
RunTest(
    [
      kWasmF32, kWasmF64, kWasmF64, kWasmF32, kWasmI32, kWasmI64, kWasmF32,
      kWasmF64, kWasmF64, kWasmF64, kWasmF32, kWasmF32
    ],
    [kWasmI64, kWasmI64, kWasmI32, kWasmF32]);

for (let i = 0; i < (debug ? 200 : 2); ++i) {
  GenerateAndRunTest();
}

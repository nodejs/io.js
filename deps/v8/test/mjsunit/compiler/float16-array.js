// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-always-turbofan --turbofan --js-staging
// Flags: --allow-natives-syntax

function storeAndLoad(x) {
  var a = new Float16Array(1)
  a[0] = x
  return a[0]
}

%PrepareFunctionForOptimization(storeAndLoad);

function assertAlmostEquals(expected, actual, epsilon = 1e-4) {
  assertTrue(
    Math.abs(expected - actual) < epsilon,
    `value difference bigger than ${epsilon} got ${actual} expected ${expected}`
  )
}

const testCases = [
  { input: 0, expected: 0 },
  { input: 0.1, expected: 0.1 },
  { input: 0.01, expected: 0.01 },
  { input: 0.001, expected: 0.001 },
  { input: 0.0001, expected: 0.0001 },
  { input: 0.00001, expected: 0.00001 },
  { input: 12, expected: 12 },
  { input: 32, expected: 32 },
  { input: 123, expected: 123 },
  { input: 12.12, expected: 12.12, epsilon: 0.01 },
  { input: 32.333, expected: 32.333, epsilon: 0.015 },
  { input: 123.11, expected: 123.11, epsilon: 0.0151 },
];

testCases.forEach(({ input, expected, epsilon }) => {
  assertAlmostEquals(expected, storeAndLoad(input), epsilon);
});

testCases.forEach(({ input, expected, epsilon }) => {
  %OptimizeFunctionOnNextCall(storeAndLoad);
  assertAlmostEquals(expected, storeAndLoad(input), epsilon);

  assertOptimized(storeAndLoad);
});

const edgeCases = [
  // an integer which rounds down under ties-to-even when cast to float16
  { input: 2049, expected: 2048 },
  // an integer which rounds up under ties-to-even when cast to float16
  { input: 2051, expected: 2052 },
  // smallest normal float16
  { input: 0.00006103515625, expected: 0.00006103515625 },
  // largest subnormal float16
  { input: 0.00006097555160522461, expected: 0.00006097555160522461 },
  // smallest float16
  { input: 5.960464477539063e-8, expected: 5.960464477539063e-8 },
  // largest double which rounds to 0 when cast to float16
  { input: 2.9802322387695312e-8, expected: 0 },
  // smallest double which does not round to 0 when cast to float16
  { input: 2.980232238769532e-8, expected: 5.960464477539063e-8 },
  // a double which rounds up to a subnormal under ties-to-even when cast to float16
  { input: 8.940696716308594e-8, expected: 1.1920928955078125e-7 },
  // a double which rounds down to a subnormal under ties-to-even when cast to float16
  { input: 1.4901161193847656e-7, expected: 1.1920928955078125e-7 },
  // the next double above the one on the previous line one
  { input: 1.490116119384766e-7, expected: 1.7881393432617188e-7 },
  // max finite float16
  { input: 65504, expected: 65504 },
  // largest double which does not round to infinity when cast to float16
  { input: 65519.99999999999, expected: 65504 },
  // lowest negative double which does not round to infinity when cast to float16
  { input: -65519.99999999999, expected: -65504 },
  // smallest double which rounds to a non-subnormal when cast to float16
  { input: 0.000061005353927612305, expected: 0.00006103515625 },
  // largest double which rounds to a subnormal when cast to float16
  { input: 0.0000610053539276123, expected: 0.00006097555160522461 },
  { input: NaN, expected: NaN },
  { input: Infinity, expected: Infinity },
  { input: -Infinity, expected: -Infinity },
  // smallest double which rounds to infinity when cast to float16
  { input: 65520, expected: Infinity },
  { input: -65520, expected: - Infinity },

];

edgeCases.forEach(({ input, expected }) => {
  assertEquals(expected, storeAndLoad(input));
});

edgeCases.forEach(({ input, expected }) => {
  %OptimizeFunctionOnNextCall(storeAndLoad);
  assertEquals(expected, storeAndLoad(input));
  assertOptimized(storeAndLoad);
});

const source = new Float16Array([0.1, 0.2, 0.3])
const copied = new Float32Array(source)

for (let i = 0; i < source.length; i++) {
  assertAlmostEquals(source[i], copied[i])
}

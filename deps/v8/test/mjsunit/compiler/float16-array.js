// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --js-float16array

function storeAndLoad (x) {
  var a = new Float16Array(1)
  a[0] = x
  return a[0]
}
function assertAlmostEquals (expected, actual, epsilon = 1e-4) {
  assertTrue(
    Math.abs(expected - actual) < epsilon,
    `value difference bigger than ${epsilon} got ${actual} expected ${expected}`
  )
}

assertAlmostEquals(0, storeAndLoad(0))

assertAlmostEquals(0.1, storeAndLoad(0.1))
assertAlmostEquals(0.01, storeAndLoad(0.01))
assertAlmostEquals(0.001, storeAndLoad(0.001))
assertAlmostEquals(0.0001, storeAndLoad(0.0001))
assertAlmostEquals(0.00001, storeAndLoad(0.00001))

assertAlmostEquals(12, storeAndLoad(12))
assertAlmostEquals(32, storeAndLoad(32))
assertAlmostEquals(123, storeAndLoad(123))

assertAlmostEquals(12.12, storeAndLoad(12.12), 0.01)
assertAlmostEquals(32.333, storeAndLoad(32.333), 0.015)
assertAlmostEquals(123.11, storeAndLoad(123.11), 0.0151)

assertEquals(NaN, storeAndLoad(NaN))
assertEquals(Infinity, storeAndLoad(Infinity))
assertEquals(-Infinity, storeAndLoad(-Infinity))

const source = new Float16Array([0.1, 0.2, 0.3])
const copied = new Float32Array(source)

for (let i = 0; i < source.length; i++) {
  assertAlmostEquals(source[i], copied[i])
}

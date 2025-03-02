// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --mock-arraybuffer-allocator
// Flags: --mock-arraybuffer-allocator-limit=65536
// Flags: --allow-natives-syntax

TestArrayBufferTooLarge();

%PrepareFunctionForOptimization(TestArrayBufferTooLarge);

TestArrayBufferTooLarge();

%OptimizeFunctionOnNextCall(TestArrayBufferTooLarge);

TestArrayBufferTooLarge();

function TestArrayBufferTooLarge() {
  const LIMIT = 65536;

  // Not too big.
  new ArrayBuffer(LIMIT);
  // Too big.
  assertThrows(() => new ArrayBuffer(LIMIT + 1), RangeError);

  // Not too big.
  new Int8Array(LIMIT);
  // Too big.
  assertThrows(() => new Int8Array(LIMIT + 1), RangeError);

  // Not too big.
  new Int16Array(LIMIT / 2);
  // Too big.
  assertThrows(() => new Int16Array(LIMIT / 2 + 1), RangeError);

  // Not too big.
  new Int32Array(LIMIT / 4);
  // Too big.
  assertThrows(() => new Int32Array(LIMIT / 4 + 1), RangeError);

  // Not too big.
  new Float64Array(LIMIT / 8);
  // Too big.
  assertThrows(() => new Float64Array(LIMIT / 8 + 1), RangeError);

  // Not too big, based on an iterable input.
  let array = new Array(LIMIT);
  for (i = 0; i < array.length; i++) array[i] = i & 0xff;

  new Int8Array(array);

  // Too big, based on an iterable input.
  let big = new Array(LIMIT + 1);
  for (i = 0; i < big.length; i++) big[i] = i & 0xff;

  assertThrows(() => new Int8Array(big), RangeError);

  assertThrowsWithMessage("Invalid typed array length", () => {
    const int8_array = new Int8Array(LIMIT + 1);
  });

  const int8_array = new Int8Array(LIMIT);
  assertThrowsWithMessage("Array buffer allocation failed", () => {
    // Outer one is still alive so we can't allocate another.
    // 64 is the default of v8_typed_array_max_size_in_heap - below this
    // level the mock array buffer allocator is not asked.
    int8_slice = new Int8Array(65);
  });

  assertThrowsWithMessage("Array buffer allocation failed", () => {
    // Outer one is still alive so we can't allocate another with slice.
    int8_slice = int8_array.slice(0, 65);
  });
}

function assertThrowsWithMessage(expected_message, fn) {
  var threw = true;
  try {
    fn();
    threw = false;
  } catch (e) {
    assertTrue(e instanceof RangeError)
    if (!e.message.includes(expected_message)) {
      print("Error '" + e.message + "' doesn't include expected '" + expected_message + "'");
    }
    assertTrue(e.message.includes(expected_message));
  }
  assertTrue(threw);
}

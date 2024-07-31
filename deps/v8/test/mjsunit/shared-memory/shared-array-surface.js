// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --shared-string-table --harmony-struct

'use strict';

(function TestNoPrototype() {
  // For now the experimental shared arrays don't have a prototype.
  assertNull(Object.getPrototypeOf(new SharedArray(10)));

  assertNull(SharedArray.prototype);

  assertThrows(() => {
    SharedArray.prototype = {};
  });
})();

(function TestPrimitives() {
  // All primitives can be stored in fields.
  let test_values =
      [42, -0, Math.random(), undefined, null, true, false, 'foo'];
  let arr = new SharedArray(test_values.length);
  let i = 0;
  for (let prim of test_values) {
    arr[i] = prim;
    assertEquals(arr[i], prim);
    i++;
  }
})();

(function TestObjects() {
  let arr = new SharedArray(5);
  // Shared objects cannot point to non-shared objects.
  assertThrows(() => {
    arr[0] = [];
  });
  assertThrows(() => {
    arr[1] = {};
  });
  // Shared objects can point to other shared objects.
  let S = new SharedStructType(['field']);
  let shared_rhs = new S();
  arr[3] = shared_rhs;
  assertEquals(arr[3], shared_rhs);
  shared_rhs = new SharedArray(10)
  arr[4] = shared_rhs;
  assertEquals(arr[4], shared_rhs);
})();

(function TestNotExtensible() {
  let arr = new SharedArray(1);
  // Shared structs are non-extensible.
  assertThrows(() => {
    arr[1] = 42;
  });
  assertThrows(() => {
    Object.setPrototypeOf(arr, {});
  });
  assertThrows(() => {
    Object.defineProperty(arr, 'nonExistent', {value: 42});
  });
})();

(function TestBounds() {
  assertThrows(
      () => {
        new SharedArray(2 ** 32);
      },
      RangeError,
      'SharedArray length out of range');

  assertThrows(
      () => {
        new SharedArray(-1);
      },
      RangeError,
      'SharedArray length out of range');
})();

(function TestOwnPropertyEnumeration() {
  let shared_array = new SharedArray(2);
  shared_array[0] = 42;

  assertEquals(2, shared_array.length);

  let propDescs = Object.getOwnPropertyDescriptors(shared_array);
  let desc = propDescs[0];
  assertEquals(true, desc.writable);
  assertEquals(false, desc.configurable);
  assertEquals(true, desc.enumerable);
  assertEquals(42, desc.value);

  let vals = Object.values(shared_array);
  assertArrayEquals([42, undefined], vals);

  let entries = Object.entries(shared_array);
  assertEquals(2, entries.length);
  assertArrayEquals(['0', 42], entries[0]);
})();

(function TestForIn() {
  let shared_array = new SharedArray(2);
  let i = 0;
  for (let index in shared_array) {
    assertEquals(index, String(i));
    i++;
  }
})();

(function TestProxyLengthGetter() {
  let a = new SharedArray(2);
  let proxy = new Proxy(a, {});
  assertEquals(2, proxy.length);
})();

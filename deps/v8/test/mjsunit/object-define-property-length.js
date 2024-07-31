// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests the object.defineProperty method - ES 15.2.3.6

var arr = new Array()
arr[1] = 'foo'
arr[2] = 'bar'
arr[3] = '42'
arr[4] = '43'
arr[5] = '44'

// ES6 9.4.2.1
// If P is 'length', than ArraySetLength(A, Desc).

assertThrows(
  () => Object.defineProperty(arr, 'length', { value: -1, configurable: true }),
  RangeError,
  'Invalid array length'
)
assertThrows(
  () =>
    Object.defineProperty(arr, 'len' + 'gth', {
      value: -1,
      configurable: true
    }),
  RangeError,
  'Invalid array length'
)

assertThrows(
  () => Object.defineProperty(arr, 'length', { value: 1, configurable: true }),
  TypeError,
  'Cannot redefine property: length'
)

Object.defineProperty(arr, 'length', { value: 10 })
desc = Object.getOwnPropertyDescriptor(arr, 'length')
assertEquals(desc.value, 10)
assertTrue(desc.writable)
assertFalse(desc.enumerable)
assertFalse(desc.configurable)

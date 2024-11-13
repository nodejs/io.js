// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

assertTrue(%NoElementsProtector());

// Test holey elements that also trips the elements-on-Array-prototype
// protector.
let arr = [0, 1, 2, 3, , , , , 8, 9];
let grouped = Object.groupBy(arr, () => {
  Array.prototype[5] = 42;
});
assertFalse(%NoElementsProtector());
// 'undefined' is the only group.
assertArrayEquals(['undefined'], Object.getOwnPropertyNames(grouped));

let expectedGrouped = [0, 1, 2, 3, undefined, 42, undefined, undefined, 8, 9];
assertArrayEquals(expectedGrouped, grouped['undefined']);
delete Array.prototype[5];

// Test slow elements that also trips the elements-on-Arr2ay-prototype
// protector.
let arr2 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
let grouped2 = Object.groupBy(arr2, () => {
  delete arr2[4];
  delete arr2[5];
  delete arr2[6];
  delete arr2[7];
  Array.prototype[14] = 42;
  arr2.length = 20;
});
// 'undefined' is the only group.
assertArrayEquals(['undefined'], Object.getOwnPropertyNames(grouped2));

let expectedGrouped2 = [
  0,         1,         2,         3,         undefined, undefined, undefined,
  undefined, 8,         9,         undefined, undefined, undefined, undefined,
  42,        undefined, undefined, undefined, undefined, undefined
];
assertArrayEquals(expectedGrouped2, grouped2['undefined']);

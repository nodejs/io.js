// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test OOB indexing on fast path.
let arr1 = [];
for (let i = 0; i < 32; i++) arr1.push(i);
let popped = false;
let grouped1 = Object.groupBy(arr1, () => {
  // Pop all of the elements to trigger right-trimming of the elements
  // FixedArray.
  for (let i = 0, len = arr1.length; i < len; i++) {
    arr1.pop();
  }
});
// 'undefined' is the only group.
assertArrayEquals(['undefined'], Object.getOwnPropertyNames(grouped1));
// 0 the only value in the group because the grouping function pops the entire
// array.
let expectedGrouped1 = [0];
assertArrayEquals(expectedGrouped1, grouped1['undefined']);

// Test result ElementsKind deduction on fast path.
//
// Initial Smi array, but due to length truncation result is not a Smi array.
let arr2 = [0,1,2,3,4,5,6,7,8,9];
let grouped2 = Object.groupBy(arr2, () => { arr2.length = 2; });
// 'undefined' is the only group.
assertArrayEquals(['undefined'], Object.getOwnPropertyNames(grouped2));
// 0,1 are the only values in the group because the source array gets truncated
// to length 2.
let expectedGrouped2 = [0,1];
assertArrayEquals(expectedGrouped2, grouped2['undefined']);

// Test holey elements
let arr3 = [0, 1, 2, 3, , , , , 8, 9];
let grouped3 = Object.groupBy(arr3, () => {});
// 'undefined' is the only group.
assertArrayEquals(['undefined'], Object.getOwnPropertyNames(grouped3));

let expectedGrouped3 =
    [0, 1, 2, 3, undefined, undefined, undefined, undefined, 8, 9];
assertArrayEquals(expectedGrouped3, grouped3['undefined']);

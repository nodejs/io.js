// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-array-from-async

async function* asyncGen(n) {
  for (let i = 0; i < n; i++) yield i * 2;
}

function* gen() {
  yield 42;
  yield 43;
  yield 44;
}

(async function TestArrayFromAsync() {
  const arr = await Array.fromAsync(asyncGen(4));
  assertEquals([0, 2, 4, 6], arr);
})();

(async function TestArrayFromAsyncSyncIterator() {
  const arr = await Array.fromAsync(gen());
  assertEquals([42, 43, 44], arr);
})();

(async function TestArrayFromAsyncWithMapping() {
  const arr = await Array.fromAsync(asyncGen(4), v => v **2);
  assertEquals([0, 4, 16, 36], arr);
})();

(async function TestArrayFromAsyncSyncIteratorWithMapping() {
  const arr = await Array.fromAsync(gen(), v => v + 4);
  assertEquals([46, 47, 48], arr);
})();

(async function TestArrayFromAsyncSyncArrayWithMapping() {
  const arr = await Array.fromAsync([1, 2, 3], v => v + 4);
  assertEquals([5, 6, 7], arr);
})();

(async function TestArrayFromAsyncArrayLike() {
  const arrayLike = {
    length: 3,
    0: 1,
    1: 2,
    2: 3
  };
  const arr = await Array.fromAsync(arrayLike);
  assertEquals([1, 2, 3], arr);
})();

(async function TestArrayFromAsyncArrayLikeWithMapping() {
  const arrayLike = {
    length: 3,
    0: 1,
    1: 2,
    2: 3
  };
  const arr = await Array.fromAsync(arrayLike, v => v **2);
  assertEquals([1, 4, 9], arr);
})();

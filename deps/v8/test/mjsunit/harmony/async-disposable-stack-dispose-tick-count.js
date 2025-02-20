// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --js-staging

let values = [];

(async () => {
  for (let i = 0; i < 10; ++i) {
    values.push(i);
    await 0;
  }
})();

async function TestCountTicks() {
  let stack = new AsyncDisposableStack();
  const firstDisposable = {
    value: 1,
    [Symbol.asyncDispose]() {
        values.push(42);
    }
  };
  const secondDisposable = {
    value: 2,
    [Symbol.asyncDispose]() {
        values.push(43);
    }
  };
  stack.use(firstDisposable);
  stack.use(secondDisposable);
  await stack.disposeAsync();
  values.push(44);
}

async function RunTest() {
  await TestCountTicks();
  assertSame('0,43,1,42,2,3,44,4', values.join(','));
}

RunTest();

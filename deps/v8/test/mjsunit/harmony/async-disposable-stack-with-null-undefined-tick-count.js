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
  values.push(42);
  let stack = new AsyncDisposableStack();
  stack.use(null);
  stack.use(undefined);
  stack.use(null);
  stack.use(undefined);
  stack.use(null);
  stack.use(undefined);
  await stack.disposeAsync();
  values.push(43);
}

async function RunTest() {
  await TestCountTicks();
  assertSame('0,42,1,2,43,3', values.join(','));
}

RunTest();

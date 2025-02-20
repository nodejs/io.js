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
  using x = {
    value: 1,
    [Symbol.dispose]() {
      values.push(42);
    }
  };
  await using y = null;
  await using z = {
    value: 1,
    [Symbol.asyncDispose]() {
      values.push(43);
    }
  };
  await using w = undefined;
  values.push(44);
}

async function RunTest() {
  await TestCountTicks();
  assertSame('0,44,43,1,42,2', values.join(','));
}

RunTest();

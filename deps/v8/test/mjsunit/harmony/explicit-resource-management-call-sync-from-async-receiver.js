// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

let value = false;

const obj =
    {
      [Symbol.dispose]() {
        value = (this === obj)
      }
    }

async function TestAwaitUsing() {
  await using x = obj;
}

async function TestAsyncDisposableStack() {
  const stack = new AsyncDisposableStack();
  stack.use(obj);
  stack[Symbol.asyncDispose]();
}

async function RunTest() {
  await TestAwaitUsing();
  assertSame(true, value);
  value = false;
  await TestAsyncDisposableStack();
  assertSame(true, value);
}

RunTest();

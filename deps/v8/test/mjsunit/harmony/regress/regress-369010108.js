// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

function loop() {
  try {
    return loop();
  } catch (e) {
    return async_function();
  }
}
assertThrowsAsync(loop(), RangeError);
async function dispose_function() {
  let stack = new AsyncDisposableStack();
  stack.use();
  await stack.disposeAsync();
}
async function async_function() {
  await dispose_function();
}

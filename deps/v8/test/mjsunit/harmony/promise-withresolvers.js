// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const desc = Object.getOwnPropertyDescriptor(Promise, 'withResolvers');
assertTrue(desc.configurable);
assertTrue(desc.writable);
assertFalse(desc.enumerable);

assertEquals(Promise.withResolvers.name, 'withResolvers');
assertEquals(Promise.withResolvers.length, 0);

{
  let called = false;
  const {promise, resolve, reject} = Promise.withResolvers();
  promise.then(v => {
    assertEquals(v, 'resolved');
    called = true;
  });
  resolve('resolved');
  assertFalse(called);
  %PerformMicrotaskCheckpoint();
  assertTrue(called);
}

{
  let called = false;
  const {promise, resolve, reject} = Promise.withResolvers();
  promise.then(v => {}, v => {
    assertEquals(v, 'rejected');
    called = true;
  });
  reject('rejected');
  assertFalse(called);
  %PerformMicrotaskCheckpoint();
  assertTrue(called);
}

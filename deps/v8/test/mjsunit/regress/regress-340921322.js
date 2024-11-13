// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

try {
  const obj = { 'i': 1096432812 };
  const arr = new Int8Array(obj.i+1);

  function foo() {
      arr[obj.i];
  }

  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
} catch (e) {
  assertTrue(e instanceof RangeError);
  assertEquals('Array buffer allocation failed', e.message);
}

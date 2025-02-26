// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  for (let i = 0; i < a.length; ++i) {
    a[i] = 1;
    if (i == 10) {
      // This is needed so that we don't OSR even if a.length is large and also
      // so that the test runs quicker.
      break;
    }
  }
  return a;
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  foo(100);
  const largeLength = 8589934592;
  foo(largeLength);

  %OptimizeFunctionOnNextCall(foo);
  const a1 = foo(100);
  assertEquals(100, a1.length);
  assertOptimized(foo);

  const a2 = foo(largeLength);
  assertEquals(largeLength, a2.length);
  assertOptimized(foo);
}

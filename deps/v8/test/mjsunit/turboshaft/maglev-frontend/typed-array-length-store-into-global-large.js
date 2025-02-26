// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

var global = 0;

function foo(size) {
  let a = new Uint8Array(size);
  global = a.length;
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  foo(100);
  const largeLength = 8589934592;
  foo(largeLength);

  %OptimizeFunctionOnNextCall(foo);
  foo(100);
  assertEquals(100, global);
  assertOptimized(foo);

  foo(largeLength);
  assertEquals(largeLength, global);
  assertOptimized(foo);
}

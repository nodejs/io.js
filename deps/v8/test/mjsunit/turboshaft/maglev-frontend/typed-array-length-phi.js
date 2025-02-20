// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size, b) {
  let a = new Uint8Array(size);
  let c = 0;
  if (b) {
    c = a.length;
  }
  return c;
}
%PrepareFunctionForOptimization(foo);

foo(100, true);
foo(100, false);

%OptimizeFunctionOnNextCall(foo);
const v1 = foo(100, true);
assertEquals(100, v1);
assertOptimized(foo);

// Also large JSTypedArray lengths are supported.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const v2 = foo(largeLength, true);
  assertEquals(largeLength, v2);
  assertOptimized(foo);
}

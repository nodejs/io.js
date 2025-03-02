// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

// Use a top-level HOLEY_SMI_ELEMENTS array so that the test function doesn't
// get confused about unexpected ElementsKinds in configs which don't have
// mementos.
let b = [];
b[10] = 0;

function foo(size) {
  let a = new Uint8Array(size);
  b[a.length] = 1;
  return b;
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  foo(100);
  const largeLength = 8589934592;
  foo(largeLength);

  %OptimizeFunctionOnNextCall(foo);
  const a1 = foo(100);
  assertEquals(1, a1[100]);
  assertOptimized(foo);

  const a2 = foo(largeLength);
  assertEquals(1, a2[largeLength]);
  assertOptimized(foo);
}

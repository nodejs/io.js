// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  return a.length;
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  foo(100);
  const largeLength = 8589934592;
  foo(largeLength);

  %OptimizeMaglevOnNextCall(foo);
  const val1 = foo(100);
  assertEquals(100, val1);
  assertTrue(isMaglevved(foo));

  const val2 = foo(largeLength);
  assertEquals(largeLength, val2);
}

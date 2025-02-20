// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  if (a.length) {
    return 1;
  }
}
%PrepareFunctionForOptimization(foo);

foo(100);
%OptimizeMaglevOnNextCall(foo);
const v1 = foo(100);
assertEquals(1, v1);
assertTrue(isMaglevved(foo));

// Also large JSTypedArray lengths are supported.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const v2 = foo(largeLength);
  assertEquals(1, v2);
  assertTrue(isMaglevved(foo));
}

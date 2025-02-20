// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  return a.length | 0xff;
}
%PrepareFunctionForOptimization(foo);

foo(100);

%OptimizeMaglevOnNextCall(foo);
const a = foo(100);
assertEquals(100 | 0xff, a);
assertTrue(isMaglevved(foo));

// Also large JSTypedArray lengths are supported.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const a2 = foo(largeLength);
  assertEquals(largeLength | 0xff, a2);
  assertTrue(isMaglevved(foo));
}

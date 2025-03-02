// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-turbofan
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

foo(100);

%OptimizeMaglevOnNextCall(foo);
const a = foo(100);
assertEquals(100, a.length);
assertTrue(isMaglevved(foo));

// If we create a large JSTypedArray (length doesn't fit in Smi), we'll deopt
// because the large length doesn't match the existing feedback for
// "i < a.length".
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const a2 = foo(largeLength);
  assertEquals(largeLength, a2.length);
  assertFalse(isMaglevved(foo));
}

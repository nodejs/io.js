// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
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

foo(100);

%OptimizeMaglevOnNextCall(foo);
const a1 = foo(100);
assertEquals(1, a1[100]);
assertTrue(isMaglevved(foo));

// If we create a large JSTypedArray (length doesn't fit in Int32), we'll deopt
// because the large length doesn't match the existing feedback for b[a.length].
if (%Is64Bit()) {
  const largeLength = 8589934592;
  const a2 = foo(largeLength);
  assertEquals(1, a2[largeLength]);
  assertFalse(isMaglevved(foo));
}

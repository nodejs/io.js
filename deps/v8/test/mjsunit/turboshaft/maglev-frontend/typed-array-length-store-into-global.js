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

foo(100);

%OptimizeFunctionOnNextCall(foo);
foo(100);
assertEquals(100, global);
assertOptimized(foo);

// If we create a large JSTypedArray (length doesn't fit in Smi), we'll deopt
// because the large length doesn't match the existing type for global.
if (%Is64Bit()) {
  const largeLength = 8589934592;
  foo(largeLength);
  assertEquals(largeLength, global);
  assertUnoptimized(foo);
}

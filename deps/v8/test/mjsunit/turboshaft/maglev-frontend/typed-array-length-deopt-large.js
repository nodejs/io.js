// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  let b = a.length;
  %DeoptimizeNow();
  return b;
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  const largeLength = 8589934592;
  assertEquals(largeLength, foo(largeLength));

  %OptimizeFunctionOnNextCall(foo);
  assertEquals(largeLength, foo(largeLength));
}

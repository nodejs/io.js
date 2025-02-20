// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  let b = new Uint8Array(size + 1);
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

  // TODO(389019544): Fix the deopt loop and enable this:
  // assertOptimized(foo);
  assertUnoptimized(foo);  // This will fail when the issue is fixed.
}

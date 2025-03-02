// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  return (+a.length > 0);
}
%PrepareFunctionForOptimization(foo);

if (%Is64Bit()) {
  foo(100);
  const largeLength = 8589934592;
  foo(largeLength);

  %OptimizeMaglevOnNextCall(foo);
  const v1 = foo(100);
  assertTrue(v1);
  assertTrue(isMaglevved(foo));

  const v2 = foo(largeLength);
  assertTrue(v2);
  assertTrue(isMaglevved(foo));
}

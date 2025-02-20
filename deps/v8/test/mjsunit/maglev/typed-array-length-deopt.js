// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(size) {
  let a = new Uint8Array(size);
  let b = a.length;
  %DeoptimizeNow();
  return b;
}

%PrepareFunctionForOptimization(foo);
assertEquals(100, foo(100));

%OptimizeMaglevOnNextCall(foo);
assertEquals(100, foo(100));

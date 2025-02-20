// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan
// Flags: --typed-array-length-loading

function foo(a, c) {
  let int32 = a + 1; // Int32 representation
  let ta = new Uint8Array(100);
  let int32_phi = c ? int32 : 42; // Int32 phi
  let len = ta.length; // IntPtr representation
  // Int32 phi and an IntPtr flowing to the same phi; we need to
  // ensure the Int32 phi will be tagged at this point.
  let phi = c ? int32_phi : len;
  return phi + 2;
}

%PrepareFunctionForOptimization(foo);

foo(100, true);
foo(100, false);

%OptimizeMaglevOnNextCall(foo);

foo(100, true);

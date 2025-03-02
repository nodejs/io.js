// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function poly_store(o, v) {
  o[0] = v;
}
%PrepareFunctionForOptimization(poly_store);

// PACKED_SMI_ELEMENTS
let o1 = [1, 2, 3];

// PACKED_DOUBLE_ELEMENTs
let o2 = [0.5, 2, 3];

poly_store(o2, 5);
poly_store(o1, 5);

%OptimizeFunctionOnNextCall(poly_store);

poly_store(o1, 5);

assertOptimized(poly_store);

// Pass a smi as a receiver - this should not crash.
poly_store(3, 3);

assertUnoptimized(poly_store);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

y = -BigInt("9223372036854775808")
function b() {
    let x = BigInt.asUintN(64, -1n);
    return BigInt.asIntN(64, x << y);
}

%PrepareFunctionForOptimization(b);
assertEquals(0n, b());
%OptimizeFunctionOnNextCall(b);
assertEquals(0n, b());

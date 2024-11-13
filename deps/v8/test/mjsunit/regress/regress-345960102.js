// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

y = BigInt("0xffffffffffffffff");

function test() {
    let x = BigInt.asIntN(64, -1n);
    let result = x >> (y);
    return BigInt.asIntN(64, result);
}

%PrepareFunctionForOptimization(test);
assertEquals(-1n, test());
assertEquals(-1n, test());
%OptimizeFunctionOnNextCall(test)
assertEquals(-1n, test());

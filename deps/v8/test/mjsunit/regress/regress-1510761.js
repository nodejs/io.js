// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Note: The turboshaft-assert-types is needed (at least for these reproducer
// functions) to reach the VisitInt64XXXWithOverflow during instruction
// selection.
//
// Flags: --allow-natives-syntax --turboshaft-assert-types

function add(a, b) {
    return a + b;
}

function sub(a, b) {
    return a - b;
}

function mul(a, b) {
    return a * b;
}

let int64Max = 9223372036854775807n;

%PrepareFunctionForOptimization(add);
assertEquals(3n, add(1n, 2n));
%OptimizeFunctionOnNextCall(add);
assertEquals(3n, add(1n, 2n));
// This will cause an overflow on the int64.
assertEquals(int64Max + int64Max, add(int64Max, int64Max));

%PrepareFunctionForOptimization(sub);
assertEquals(-1n, sub(1n, 2n));
%OptimizeFunctionOnNextCall(sub);
assertEquals(-1n, sub(1n, 2n));
// This will cause an overflow on the int64.
assertEquals(int64Max + int64Max, sub(int64Max, -int64Max));

%PrepareFunctionForOptimization(mul);
assertEquals(6n, mul(2n, 3n));
%OptimizeFunctionOnNextCall(mul);
assertEquals(6n, mul(2n, 3n));
// This will cause an overflow on the int64.
assertEquals(int64Max * int64Max, mul(int64Max, int64Max));

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let a = 0;
let b = 1;
let c = 2;
let d = 3;

function read_a() { return a; }
function read_b1() { return b; }
function read_b2() { return b; }
function read_c() { return c; }
function read_d1() { return d; }
function read_d2() { return d; }

%PrepareFunctionForOptimization(read_a);
assertEquals(0, read_a());

%OptimizeFunctionOnNextCall(read_a);
assertEquals(0, read_a());
assertOptimized(read_a);

%PrepareFunctionForOptimization(read_b1);
%PrepareFunctionForOptimization(read_b2);
assertEquals(1, read_b1());
assertEquals(1, read_b2());

%OptimizeFunctionOnNextCall(read_b1);
%OptimizeFunctionOnNextCall(read_b2);

assertEquals(1, read_b1());
assertEquals(1, read_b2());
assertOptimized(read_b1);
assertOptimized(read_b2);

%PrepareFunctionForOptimization(read_c);
assertEquals(2, read_c());

%OptimizeFunctionOnNextCall(read_c);
assertEquals(2, read_c());
assertOptimized(read_c);

%PrepareFunctionForOptimization(read_d1);
%PrepareFunctionForOptimization(read_d2);
assertEquals(3, read_d1());
assertEquals(3, read_d2());

%OptimizeFunctionOnNextCall(read_d1);
%OptimizeFunctionOnNextCall(read_d2);

assertEquals(3, read_d1());
assertEquals(3, read_d2());
assertOptimized(read_d1);
assertOptimized(read_d2);

a = 4;

assertUnoptimized(read_a);
assertEquals(4, read_a());

assertOptimized(read_b1);
assertOptimized(read_b2);
assertOptimized(read_c);
assertOptimized(read_d1);
assertOptimized(read_d2);

b = 5;

assertUnoptimized(read_b1);
assertUnoptimized(read_b2);
assertEquals(5, read_b1());
assertEquals(5, read_b2());

assertOptimized(read_c);
assertOptimized(read_d1);
assertOptimized(read_d2);

c = 6;

assertUnoptimized(read_c);
assertEquals(6, read_c());

assertOptimized(read_d1);
assertOptimized(read_d2);

d = 7;

assertUnoptimized(read_d1);
assertUnoptimized(read_d2);

assertEquals(7, read_d1());
assertEquals(7, read_d2());

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

let val1 = 0;
let val2 = 0;

function read1() {
  return val1;
}

function read2() {
  return val2;
}

%PrepareFunctionForOptimization(read1);
%PrepareFunctionForOptimization(read2);
read1();
read2();

%OptimizeFunctionOnNextCall(read1);
%OptimizeFunctionOnNextCall(read2);

assertEquals(0, read1());
assertOptimized(read1);

assertEquals(0, read2());
assertOptimized(read2);

function write1(value) {
  // The representation of the value we're writing is kTagged
  val1 = value;
}

%PrepareFunctionForOptimization(write1);
write1(0);
%OptimizeMaglevOnNextCall(write1);
assertOptimized(read1);

// This deopts `write1`, then invalidates the constness which deopts `read1`.
write1(10);

assertUnoptimized(write1);
assertUnoptimized(read1);
assertEquals(10, read1());

function write2(value) {
  // The representation of the value we're writing is kInt32.
  val2 = value + 1;
}

%PrepareFunctionForOptimization(write2);
write2(-1);
%OptimizeMaglevOnNextCall(write2);
assertOptimized(read2);

// This deopts `write2`, then invalidates the constness which deopts `read2`.
write2(10);

assertUnoptimized(write2);
assertUnoptimized(read2);
assertEquals(11, read2());

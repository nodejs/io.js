// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-frontend --turbofan
// Flags: --no-always-turbofan --no-stress-concurrent-inlining

function simple1() {
  return 0;
}

%PrepareFunctionForOptimization(simple1);
assertEquals(0, simple1());
%OptimizeFunctionOnNextCall(simple1);
assertEquals(0, simple1());

function simple2(x) {
  return x + 2;
}

%PrepareFunctionForOptimization(simple2);
assertEquals(5, simple2(3));
%OptimizeFunctionOnNextCall(simple2);
assertEquals(5, simple2(3));
assertOptimized(simple2);
assertEquals("a2", simple2("a"));
assertUnoptimized(simple2);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

var glob = NaN;

function foo(val) {
  glob = val;
}

%PrepareFunctionForOptimization(foo);
foo(NaN);

%OptimizeFunctionOnNextCall(foo);
foo(NaN);
assertEquals(NaN, glob);
assertOptimized(foo);

foo(42);
assertEquals(42, glob);
assertUnoptimized(foo);

// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --nolazy-feedback-allocation
// Flags: --stress-concurrent-inlining

function f() {}
let array = [];
array.length = 65525;
let bound = f.bind(null, ...array);
function g() { bound(1); }
%PrepareFunctionForOptimization(g);
g();
%OptimizeFunctionOnNextCall(g);
g()

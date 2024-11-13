// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

%NeverOptimizeFunction(h);
function h(x) { return x; }
function g(x) { return h(x); }
function f(x) { return g(x); }

%PrepareFunctionForOptimization(g);
%PrepareFunctionForOptimization(f);
assertEquals(42, f(42));
assertEquals(42, f(42));
%OptimizeFunctionOnNextCall(f);
assertEquals(42, f(42));
assertOptimized(f);

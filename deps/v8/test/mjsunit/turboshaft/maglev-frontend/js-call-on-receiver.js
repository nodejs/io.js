// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(o) { return o.x(17); }

let o = { y : 42, x : function(a) { return a + this.y; } };
%NeverOptimizeFunction(o.x);

%PrepareFunctionForOptimization(f);
assertEquals(59, f(o));
assertEquals(59, f(o));
%OptimizeFunctionOnNextCall(f);
assertEquals(59, f(o));
assertOptimized(f);

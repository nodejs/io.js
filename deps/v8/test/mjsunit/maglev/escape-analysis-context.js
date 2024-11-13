// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-escape-analysis

function soft_deopt() {}

function foo(b) {
    var x;
    if (!b) return 0;
    // We call a function here that will create a soft deopt
    // point and kill all nodes from this point on. This entails
    // that the function context will have no use, so we can elide
    // its allocation.
    soft_deopt();
    let bar  = function() {
        x = 42;
    }
    bar();
    return x;
}

%PrepareFunctionForOptimization(foo);
assertEquals(0, foo(false));

%OptimizeMaglevOnNextCall(foo);
assertEquals(0, foo(false));
assertTrue(isOptimized(foo));

// This will deopt and the context will be materialized during
// deoptimization.
assertEquals(42, foo(true));
assertFalse(isOptimized(foo));

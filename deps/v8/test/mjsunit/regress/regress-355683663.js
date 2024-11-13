// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo() {
    const v5 = false;
    const o6 = {
    };
    const o7 = {
    };
    o7.b = v5;
    const v8 = /7sQa[bc]d/msu;
    Symbol.a = Symbol;
    const o10 = {
    };
    for (const v11 in Symbol) {
        o10[v11] = 2062;
        v8.b = /f/yvms;
        function f13() {
            return v8;
        }
    }
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();

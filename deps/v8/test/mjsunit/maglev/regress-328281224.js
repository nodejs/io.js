// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-maglev-inlining

function f() {}

function foo() {
    var x;
    try {
        x = [1, 2, 3];
        f(); // f can throw, so we will need to re-materialize x.
        x = undefined; // we need to mutate x, otherwise the lifetime
                       // of the object goes until the return, the array
                       // has a load-use and we do not reproduce the crash.
        f(); // call forces to trash all registers in regalloc.
    } catch(e) {
    }
    return x;
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

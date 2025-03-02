// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let o0 = {};
function f1() {
    function f2() {
    }
    function F4( a8) {
        function f10() {
            a8 = f2;
        }
        o0 = a8;
    }
    %PrepareFunctionForOptimization(f2);
    %PrepareFunctionForOptimization(F4);
    new F4();
}
%PrepareFunctionForOptimization(f1);
f1();
f1();
%OptimizeFunctionOnNextCall(f1);
f1();

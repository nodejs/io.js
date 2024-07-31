// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --always-turbofan
// Flags: --turboshaft-assert-types --no-turbo-loop-peeling

function f0() {
    for (let i3 = 0; i3 <= -65535;) {
        i3--;
        function F9() {
        }
    }
}
%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();

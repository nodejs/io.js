// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft

function f0(a1) {
    let v2 = 0;
    v2++;
    let v4 = -v2;
    function F5() {
        this.d = v2;
    }
    let v11 = -2147483648;
    if (a1) {
        v4 = -1;
        v11 = 1;
    }
    return (v4 - v11) == -2147483648;
}

%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();

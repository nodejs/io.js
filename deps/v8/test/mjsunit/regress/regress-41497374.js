// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

const v1 = new Set();
function F2(a4, a5) {
    if (!new.target) { throw 'must be called with new'; }
    function f6(a7) {
        let v9 = v1 | 0;
        v9--;
        const v15 = Math.max(v9 >>> (a7 ** 52));
        const v18 = (-7462) ** 52;
        let v19 = Math.min(v15, v18);
        v19++;
        const o21 = {
            "_divider": v19,
        };
        return v18;
    }
    %PrepareFunctionForOptimization(f6);
    f6();
    const v22 = %OptimizeFunctionOnNextCall(f6);
    f6();
}
const v24 = new F2(F2, F2);
new F2(F2, v24);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function f0() {
    for (let i2 = 0;
        (() => {
            i2--;
            let v4 = 1;
            v4--;
            return i2 < v4;
        })();
        (() => {
            const v8 = i2++;
            const v9 = v8 + v8;
            const v11 = Intl.NumberFormat;
            v11();
            v9 & v9;
            ++i2;
        })()) {
        const v17 = [-5n];
        for (const v18 of v17) {
            v17[100] = 1;
        }
    }
}
const v20 = %PrepareFunctionForOptimization(f0);
f0();
const v22 = %OptimizeFunctionOnNextCall(f0);
f0();

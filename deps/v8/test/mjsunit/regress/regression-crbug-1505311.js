// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f0() {
    let v1 = 0;
    let v3 = -1024;
    for (let v4 = 0; v4 < 5; v4++) {
        const v8 =v1 - 55598 | 0;
        const v10 = v1 + 2147483647;
        let v11 = v10 | f0;
        v1 = v11;
        const v15 = ((v3 & v10) && v8) | 0;
            const v23 = %OptimizeOsr();
        for (let i = 0; i < 5; i++) {
            v11++;
        }
        v3 = v15;
    }
}
%PrepareFunctionForOptimization(f0);
f0();


function f0() {
    let v1 = 0;
    let v3 = -1024;
    for (let v4 = 0; v4 < 5; v4++) {
        const v8 =v1 - 55598 | 0;
        const v10 = v1 + 2147483647;
        v1 = v10 | f0;
        const v15 = ((v3 & v10) && v8) | 0;
        let v16 = %OptimizeOsr();
        for (let v17 = 0; v17 < 5; v17++) {
            v16++;
        }
        v3 = v15;
    }
}
%PrepareFunctionForOptimization(f0);
f0();

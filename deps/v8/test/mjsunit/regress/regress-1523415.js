// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-instruction-selection
// Flags: --jit-fuzzing

const v1 = new Float32Array();
function f2(a3, a4) {
    const v5 = a3[0];
    const v6 = a3[2];
    const v7 = a3[3];
    const v8 = a3[4];
    const v9 = a3[5];
    const v10 = a3[6];
    const v11 = a3[7];
    const v12 = a3[8];
    const v13 = a3[9];
    const v14 = a3[10];
    const v15 = a3[11];
    const v16 = a3[12];
    const v17 = a3[536870912];
    const v18 = a3[36];
    return (((((((((((((a4 + v5) & v18) + v6) + v7) + v8) + v9) && v10) + v11) + v12) + v13) + v14) + v15) + v16) + v17;
}
%PrepareFunctionForOptimization(f2);
f2(v1);
f2(v1);
const v35 = %OptimizeFunctionOnNextCall(f2);
f2(v1);

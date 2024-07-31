// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

const v11 = new Promise(()=>{});
for (let v12 = 0; v12 < 5; v12++) {
    async function* f13( a15) {
        for (let v16 = 0; v16 < 5; v16++) {
            if (v12 == 4 && v16 == 4) {
                %OptimizeOsr();
            }
            for (const v17 in v11) {
                yield* SharedArrayBuffer;
            }
        }
        for (let v24 = 0; v24 < 5; v24++) {
            Math.acosh(a15 %= 4096 % v24);
        }
        return f13;
    }
    if (v12 == 0) {
      %PrepareFunctionForOptimization(f13);
    }
    f13().next();
}

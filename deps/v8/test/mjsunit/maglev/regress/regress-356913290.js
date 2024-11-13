// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax


let v8 = -2147483649;
function f9() {
    let v10 = 0;
    let v12 = -1024;
    for (let v13 = 0; v13 < 25; v13++) {
        let v16 =v10 - 5 | v8;
        const v18 = v10 + 2147483647;
        v10 = v18 | f9;
        v8 = v13;
        v16++;
        v12 = ((v12 & v18) && v16) | 0;
    }
}
%PrepareFunctionForOptimization(f9);
f9();
%OptimizeFunctionOnNextCall(f9);
f9();

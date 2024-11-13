// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

const o1 = {
    "type": "function",
};
function f2() {
    for (let v5 = 0; v5 < 5; v5++) {
        v5 **= 256;
        const o6 = {
            "type": v5,
        };
        const o8 = {
            get b() {
            },
        };
    }
}
%PrepareFunctionForOptimization(f2);
f2(f2());
%OptimizeFunctionOnNextCall(f2);
f2();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let x = 1;
x = 0;

function opt() {
    let v4 = 0;
    for (let i = 0; i < 10; i++) {
        for(let j=0; j<6; j++) {
            switch(v4) {
                case 0:
                    v4 = 1;
                    break;
                case 1:
                    v4 = 2;
                    break;
                case 2:
                    v4 = 3;
                    break;
                case 3:
                    v4 = 4;
                    break;
                case 4:
                    v4 = x;
                    break;
            };
        }
    }
    return v4;
}

%PrepareFunctionForOptimization(opt);
opt();
opt();
%OptimizeFunctionOnNextCall(opt);
opt();

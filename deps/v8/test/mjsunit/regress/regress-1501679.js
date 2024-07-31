// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --jit-fuzzing

function opt(opt_param) {
    class C3 extends Int16Array {
        set a(a5) {
            super.c = this;
            super.byteLength;
        }
    }
    const o8 = {
        "maxByteLength": 875,
    };
    const v10 = new ArrayBuffer(255, o8);
    const v11 = new C3(v10);
    v11.a = -2147483648;
}

%PrepareFunctionForOptimization(opt);
opt(true);
%OptimizeFunctionOnNextCall(opt);
opt(false);

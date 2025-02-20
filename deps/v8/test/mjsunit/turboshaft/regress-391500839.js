// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --single-threaded
// Flags: --jit-fuzzing --turboshaft-string-concat-escape-analysis

function f0() {}

class C3 {
    toString() {
        try {
            Map();
        } catch(e10) {
        }
    }
}

const v11 = new C3();

function f12(c) {
  const v13 = `setMinutes${f0}valueOf`;
    const v15 = [];
    const v16 = v11.toString;
    Reflect.apply(v16, v13, v15);
}

%PrepareFunctionForOptimization(f12);
f12();
f12();

%OptimizeFunctionOnNextCall(f12);
f12();

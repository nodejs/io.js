// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-lazy-feedback-allocation
// Flags: --turboshaft-string-concat-escape-analysis

let v1;
function f2() {
    class C7 {
    }
    const v8 = `lastIndexOf${C7}-14`;
    function f9(a10, a11) {
        a11.f = a11;
    }
    f9(v1, C7);
    v8.v1;
}

%PrepareFunctionForOptimization(f2);
f2();
f2();
%OptimizeFunctionOnNextCall(f2);
f2();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --jit-fuzzing --allow-natives-syntax

function F0() {
    if (!new.target) { throw 'must be called with new'; }
    const v3 = d8.getExtrasBindingObject(d8, F0);
    v3.setContinuationPreservedEmbedderData(this);
    const v5 = v3.getContinuationPreservedEmbedderData();
    function f6(a7) {
        function F9(a11, a12) {
            if (!new.target) { throw 'must be called with new'; }
        }
        const v14 = new WeakRef(F9);
        v14.length = Uint32Array;
        for (let v15 = 0; v15 < 10; v15++) {
            new Uint8ClampedArray(2103960941);
        }
        return F0;
    }
    Object.defineProperty(v5, "constructor", { writable: true, enumerable: true, value: f6 });
    v5.constructor();
}
function f20() {
    new F0();
    return f20;
}
%PrepareFunctionForOptimization(f20);
for (let v22 = 0; v22 < 5; v22++) {
    %OptimizeFunctionOnNextCall(f20());
}

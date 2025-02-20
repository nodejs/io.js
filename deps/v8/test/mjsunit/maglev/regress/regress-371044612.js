// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev-optimistic-peeled-loops

function f0() {
    for (let i2 = 0;
        (() => {
            return i2 < 10;
        })();
        i2++) {
        if (i2 <= NaN) {
            throw 0;
        }
    }
}
f0();
%PrepareFunctionForOptimization(f0);
f0();
%OptimizeMaglevOnNextCall(f0);
f0();

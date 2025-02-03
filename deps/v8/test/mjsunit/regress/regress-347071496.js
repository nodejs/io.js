// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

function* f2() {
    const o4 = {
        "foo": 129,
    };
    for (let i7 = 0; 128 < 10;) {
      const v13 = yield 1;
      [- v13];
    }
}
%PrepareFunctionForOptimization(f2);
f2().next();
%OptimizeMaglevOnNextCall(f2);
f2();

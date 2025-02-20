// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --script-context-mutable-heap-number
// Flags: --no-always-turbofan --turbolev

let v0 = 7;
function f1() {
    for (let v4 = 0; v4 < 5; v4++) {
        v0 >>>= v4;
    }
}

%PrepareFunctionForOptimization(f1);
f1();
f1();
%OptimizeFunctionOnNextCall(f1);
f1();

v0 = -1;
f1();
assertUnoptimized(f1);

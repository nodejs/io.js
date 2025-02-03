// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let a = [1.1, , 3.3];
function foo(i) {
    if (a[i] === null) {
        return 0;
    } else if(a[i] === undefined) {
        return 1;
    }
    return 2;
}

%PrepareFunctionForOptimization(foo);
assertEquals(2, foo(0));
assertEquals(1, foo(1));
%OptimizeFunctionOnNextCall(foo);
assertEquals(1, foo(1));

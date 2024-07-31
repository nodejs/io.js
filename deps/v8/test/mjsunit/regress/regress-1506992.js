// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --jit-fuzzing

function foo(arg) {
    [0, 0].map(()=>{
        let tmp = arg;
        arg = 1.1;
        return tmp
    })
}

%PrepareFunctionForOptimization(foo);
foo(0);
foo(0);
%OptimizeFunctionOnNextCall(foo);
foo([]);

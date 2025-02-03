// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function g_raw() {
    return arguments;
}

arr = [];
arr.length=65521;
var g = g_raw.bind(null,...arr);

function f() {
    return g();
}

%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();

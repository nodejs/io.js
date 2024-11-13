// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function f6(a7) {
    a7 >>> a7;
    const v12 = a7 == a7; // NumberOrBoolean
    v12 || v12;
}
%PrepareFunctionForOptimization(f6);

f6(false);
f6(-8.45);
%OptimizeMaglevOnNextCall(f6);
f6();

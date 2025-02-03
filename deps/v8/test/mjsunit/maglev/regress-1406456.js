// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo() {
    const buffer = new SharedArrayBuffer(1395, {
        "maxByteLength": 2110270,
    });
    const data = new DataView(buffer);
    data.setInt16();
}
%PrepareFunctionForOptimization(foo);
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

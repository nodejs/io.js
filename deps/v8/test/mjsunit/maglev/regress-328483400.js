// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo(a) {
    const o = {};
    Object.defineProperty(o, "x", { set: Math.ceil });
    o.x = a; // Creates a Math.ceil builtin continuation.
}

%PrepareFunctionForOptimization(foo);
foo({});
foo({});
%OptimizeMaglevOnNextCall(foo);
foo({});

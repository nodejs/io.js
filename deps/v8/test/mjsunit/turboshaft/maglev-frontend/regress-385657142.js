// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev

let o0 = { };
o0 = -2027262633;
const v4 = Reflect;

function f5() {
  o0 = v4;
}


%PrepareFunctionForOptimization(f5);
// not collecting feedback.

%OptimizeFunctionOnNextCall(f5);
f5();

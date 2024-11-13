// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function f() {
  let a = arguments[0] + 1;
  a.__proto__ = a;
}
%PrepareFunctionForOptimization(f);
f(1);
%OptimizeFunctionOnNextCall(f);
f(1);

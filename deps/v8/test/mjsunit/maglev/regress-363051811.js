// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  const len =[100.1,0.0,-6.632927246554519,-1.5476427391889882e+308][1];
  const i = Array(len);
  "foo"[i];
}
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();

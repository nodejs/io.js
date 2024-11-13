// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(a3,  a6) {
  a3.f = a6;
  const v8 = new Int32Array();
  v8[148] = v8;
}

function bar() {
  return foo(/xvxyz{1,32}?(ab|cde)\1eFa\S/suimyg);
}

class C0 {
}
const v1 = new C0();

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
foo(v1);

for (let v13 = 0; v13 < 5; v13++) {
  bar();
  %OptimizeFunctionOnNextCall(bar);
}

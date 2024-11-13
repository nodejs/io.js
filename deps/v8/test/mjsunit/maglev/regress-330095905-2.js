// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-escape-analysis --verify-heap

function bar() {
  return arguments;
}
function foo() {
  let v = bar();
  bar(v);
}
%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

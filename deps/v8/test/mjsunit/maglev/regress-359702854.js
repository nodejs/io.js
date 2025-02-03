// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function create(x) {
  return { "x": x };
}
function bar(o, y) {
  o.x = "string";
  o.y = y;
}
function foo(x, y) {
  const o = create(x);
  bar(o);
  return o;
}

%PrepareFunctionForOptimization(create);
%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

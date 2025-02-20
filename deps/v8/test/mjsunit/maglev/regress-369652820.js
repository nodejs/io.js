// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --expose-gc

function foo(obj, dummy) {
  obj.x = "dong";
  obj.y = undefined;
}

function bar() {
  const obj = {"x": "ding"};
  foo(obj);
  return obj['x'];
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
assertEquals(bar(), "dong");
assertEquals(bar(), "dong");

%OptimizeMaglevOnNextCall(bar);
gc();
assertEquals(bar(), "dong");

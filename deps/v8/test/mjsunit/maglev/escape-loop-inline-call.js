// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function bar(o) {
  o.x = o.x + 1;
}

function foo() {
  let o = {x : 1};
  for (let i = 0; i < 2; i++) {
    bar(o);
  }
  return o.x;
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
assertEquals(foo(), 3);
assertEquals(foo(), 3);
%OptimizeFunctionOnNextCall(foo);
assertEquals(foo(), 3);
assertEquals(foo(), 3);

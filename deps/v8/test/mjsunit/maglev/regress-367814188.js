// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-maglev-loop-peeling

let obj = { y: 19.5 };
obj.y = 3.3336; // make obj.y non-const
let arr = [-0.0, /* hole */, 2.5];

function foo(n) {
  let v = arr[1];
  for (let i = 0.5; i < n; i += 1) {
    i += v; // Float64 use for {v} so that it gets untagged.
    v = 40;
  }
  obj.y = v;
}

%PrepareFunctionForOptimization(foo);
foo(5);
assertEquals(40, obj.y);

%OptimizeFunctionOnNextCall(foo);
foo(0);
assertEquals(undefined, obj.y);

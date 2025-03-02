// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function inner_loop(x) {
  let s = 0;
  for (let i = 0; i < x; i++) {
    s += i;
  }
  return s;
}

function call_loop(x) {
  return inner_loop(x);
}

%PrepareFunctionForOptimization(inner_loop);
%PrepareFunctionForOptimization(call_loop);
assertEquals(15, call_loop(6));
%OptimizeFunctionOnNextCall(call_loop);
assertEquals(15, call_loop(6));
assertOptimized(call_loop);

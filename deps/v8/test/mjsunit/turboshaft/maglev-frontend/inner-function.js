// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function fun_with_inner(x) {
  let v = 42;
  function inner() {
    v += 3;
    return x + v;
  }
  let r1 = inner();
  v += 2;
  let r2 = inner();
  return r1 + r2;
}

%PrepareFunctionForOptimization(fun_with_inner);
assertEquals(105, fun_with_inner(5));
assertEquals(105, fun_with_inner(5));
%OptimizeFunctionOnNextCall(fun_with_inner);
assertEquals(105, fun_with_inner(5));

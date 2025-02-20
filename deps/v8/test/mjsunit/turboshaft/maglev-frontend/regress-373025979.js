// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function g() {
  return throw_because_this_function_is_not_defined();
}
%NeverOptimizeFunction(g);

function f7() {
  function f12(a13, a14) {
    let v16 = a13 >>> a14;
    try {
      new BigUint64Array();
      a14 &= v16;
      v16 = 45;
      g();
    } catch(e21) {
      v16 + 3;
    }
  }
  %PrepareFunctionForOptimization(f12);
  f12();
}


%PrepareFunctionForOptimization(f7);
f7();
f7();
%OptimizeFunctionOnNextCall(f7);
f7();

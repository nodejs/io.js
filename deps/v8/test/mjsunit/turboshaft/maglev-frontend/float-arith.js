// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function math_float(x, y) {
  let a = x * y;
  let b = a + 152.56;
  let c = b / x;
  let e = c - Math.round(y);
  let f = e % 5.56;
  let g = f ** x;
  let h = -g;
  return h;
}

%PrepareFunctionForOptimization(math_float);
math_float(4.21, 3.56);
let expected = math_float(4.21, 3.56);
%OptimizeFunctionOnNextCall(math_float);
assertEquals(expected, math_float(4.21, 3.56));
assertOptimized(math_float);

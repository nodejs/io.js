// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function add3(a, b, c) { return a + b + c; }
function add2(a, b) { return a + b; }

function call_arg(f, a, b, c) {
  return f(a, b, c);
}

%PrepareFunctionForOptimization(call_arg);
assertEquals(15, call_arg(add3, 3, 5, 7));
assertEquals(8, call_arg(add2, 3, 5, 7));
%OptimizeFunctionOnNextCall(call_arg);
assertEquals(15, call_arg(add3, 3, 5, 7));
assertEquals(8, call_arg(add2, 3, 5, 7));
assertOptimized(call_arg);

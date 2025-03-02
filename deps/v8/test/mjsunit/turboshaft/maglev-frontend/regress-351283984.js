// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo(b) {
  let p1 = b ? 1 : 12;
  let p2 = b ? p1 : 15;
  return b + 2;
}

%PrepareFunctionForOptimization(foo);
assertEquals(44, foo(42));

%OptimizeFunctionOnNextCall(foo);
assertEquals(44, foo(42));

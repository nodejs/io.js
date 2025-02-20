// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function foo() {
  for (var x in this) {}
  return x;
};

%PrepareFunctionForOptimization(foo);
let val = foo();
%OptimizeFunctionOnNextCall(foo);
assertEquals(val, foo());
assertOptimized(foo);

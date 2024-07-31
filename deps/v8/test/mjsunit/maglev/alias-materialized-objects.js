// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function bar() {}

function foo(i) {
  let a = [1, 4.2, 3];
  let b = a;
  if (i == 1) {
    bar(); // Force deoptimization.
    return a == b;
  }
}

%PrepareFunctionForOptimization(foo);
foo(0);
foo(0);
%OptimizeMaglevOnNextCall(foo);
foo(0);
assertTrue(isMaglevved(foo));
assertTrue(foo(1));
assertUnoptimized(foo);

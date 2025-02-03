// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function foo(arg) {
  try {
    Object.prototype.valueOf.apply(arg, arguments);
  } catch (e) {
    return 42;
  }
}

%PrepareFunctionForOptimization(foo);
foo(this);

%OptimizeFunctionOnNextCall(foo);
assertEquals(42, foo());

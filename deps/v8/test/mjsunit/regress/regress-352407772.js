// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  try {
    new String(Symbol());
  } catch(e) {
    return 42;
  }
}
%PrepareFunctionForOptimization(foo);
assertEquals(42, foo());
assertEquals(42, foo());
%OptimizeFunctionOnNextCall(foo);
assertEquals(42, foo());

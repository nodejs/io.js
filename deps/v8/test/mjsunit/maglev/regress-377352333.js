// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  for (let i = 0; i < 14; ++i) {
    let x = (1 / -i);
    if (i == 0) assertEquals(x, -Infinity);
  }
}

foo();
%PrepareFunctionForOptimization(foo);
%OptimizeMaglevOnNextCall(foo);
foo();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo( ...__v_21) {
  for (let __v_22 = 0; __v_22 < 1000; __v_22++) {
    if (__v_22 === 999) {
      %OptimizeOsr();
    }
    __v_21.map(foo);
    __v_21.map(foo);
  }
}

%PrepareFunctionForOptimization(foo);
foo();

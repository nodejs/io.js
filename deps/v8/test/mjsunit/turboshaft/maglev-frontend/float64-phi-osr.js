// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function foo() {
  let v1 = 3.45;

  for (let i = 0; i < 5; i++) {
    const v8 = v1 + 2.25; // Float64 use
    v1 = 2.5; // Float64 input for v1's phi

    %OptimizeOsr();
  }
}

%PrepareFunctionForOptimization(foo);
foo();

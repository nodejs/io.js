// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --jit-fuzzing

function f1(a2) {
  const o5 = {
    [a2]() {
    },
  };
}

function f2() {
  for (let v6 = 0; v6 < 5; v6++) {
    const v7 = %OptimizeOsr();
    [65537,v6].reduceRight(f1);
  }
}

%PrepareFunctionForOptimization(f2);
f2();

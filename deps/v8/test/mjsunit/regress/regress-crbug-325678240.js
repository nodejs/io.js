// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev-hoist-osr-value-phi-untagging

for (let i = 0; i < 5; i++) {
  async function* func(arg) {
    try {
      for (let j = 0; j < 1; j++) {
        if (i == 4)
          %OptimizeOsr();
        yield* SharedArrayBuffer;
      }
    } catch (e) { }
    for (let j = 0; j < 1; j++) {
      arg %= 0;
    }
  }
    %PrepareFunctionForOptimization(func);
  k = func();
  k.next();
}

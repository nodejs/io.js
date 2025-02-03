// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation --single-threaded

var e;
function foo() {
  for (let v0 = 0; v0 < 5; v0++) {
    const v1 = %OptimizeOsr();  // Force Osr'ing, v1 = undefined
    const v2 = v1 | v1;         // v2 = 0
    const v3 = v2 ** v2;        // v3 = 1 (HeapNumber on interpreter,
                                //         Smi on Maglev due to Canonicalization)
    e = v3;                     // The first time we are running the loop, we are
                                // still on the interpreter, it will set {e} is
                                // global constant with value HeapNumber 1.
                                // Maglev then adds the assumption that v3 is a
                                // HeapNumber, based on the feedback.
    for (const v4 in v3) {      // Crash in ToObject call on Maglev since we
    }                           // assume it isnot an Smi.
  }
}

%PrepareFunctionForOptimization(foo);
foo();

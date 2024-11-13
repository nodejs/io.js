// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function dematerialized_heap_number(n) {
  let f64 = 17.72;
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {f64} will thus have no
    // use besides the frame state for this deopt).
    return f64 + 5;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_heap_number);
assertEquals(7, dematerialized_heap_number(5));

%OptimizeFunctionOnNextCall(dematerialized_heap_number);
assertEquals(7, dematerialized_heap_number(5));
assertEquals(22.72, dematerialized_heap_number(-1));

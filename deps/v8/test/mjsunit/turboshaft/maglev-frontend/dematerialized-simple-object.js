// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function dematerialized_simple_object(n) {
  let o = { x : 42, y : n };
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {o} will thus have no
    // use besides the frame state for this deopt).
    n + 5;
    return o;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_simple_object);
assertEquals(7, dematerialized_simple_object(5));
assertEquals(7, dematerialized_simple_object(5));

%OptimizeFunctionOnNextCall(dematerialized_simple_object);
assertEquals(7, dematerialized_simple_object(5));
assertEquals({ x : 42, y : -1 }, dematerialized_simple_object(-1));

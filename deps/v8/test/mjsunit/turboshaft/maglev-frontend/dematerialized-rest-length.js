// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function dematerialized_rest(n, ...rest) {
  let len = rest.length;
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {len} will thus have no
    // use besides the frame state for this deopt).
    n + 5;
    return len;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_rest);
assertEquals(7, dematerialized_rest(5, 2, 3));
assertEquals(7, dematerialized_rest(5, 2, 3, 4));

%OptimizeFunctionOnNextCall(dematerialized_rest);
assertEquals(7, dematerialized_rest(5, 2, 3));

assertEquals(3, dematerialized_rest(-1, 2, 3, 4));

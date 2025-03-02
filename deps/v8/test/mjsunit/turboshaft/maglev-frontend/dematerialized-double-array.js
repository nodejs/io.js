// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function dematerialized_double_array(n) {
  let arr = [ 1.1, 2.2, 3.3 ];
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {arr} will thus have no
    // use besides the frame state for this deopt).
    n + 5;
    return arr;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_double_array);
assertEquals(7, dematerialized_double_array(5));
assertEquals(7, dematerialized_double_array(5));

%OptimizeFunctionOnNextCall(dematerialized_double_array);
assertEquals(7, dematerialized_double_array(5));
assertEquals([1.1, 2.2, 3.3], dematerialized_double_array(-1));

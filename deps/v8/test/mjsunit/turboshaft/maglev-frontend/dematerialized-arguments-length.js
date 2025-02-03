// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

let n = 5;
n = 2; // Making {n} not constant.

function dematerialized_arguments_length() {
  let len = arguments.length;
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {len} will thus have no
    // use besides the frame state for this deopt).
    n + 5;
    return len;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_arguments_length);
n = 5;
assertEquals(7, dematerialized_arguments_length(2, 3));
assertEquals(7, dematerialized_arguments_length(2, 3, 4, 9));

%OptimizeFunctionOnNextCall(dematerialized_arguments_length);
assertEquals(7, dematerialized_arguments_length(2, 3));

n = -1;
assertEquals(4, dematerialized_arguments_length(2, 3, 4, 9));

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function dematerialized_nested_object(n) {
  let o = { a : 42, b : { c : 3.75 , d : [ 4 ] } };
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {o} will thus have no use
    // besides the frame state for this deopt).
    n + 5;
    return o;
  }
  return n + 2;
}


%PrepareFunctionForOptimization(dematerialized_nested_object);
assertEquals(7, dematerialized_nested_object(5));
assertEquals(7, dematerialized_nested_object(5));

%OptimizeFunctionOnNextCall(dematerialized_nested_object);
assertEquals(7, dematerialized_nested_object(5));
assertEquals({ a : 42, b : { c : 3.75 , d : [ 4 ] } },
             dematerialized_nested_object(-1));

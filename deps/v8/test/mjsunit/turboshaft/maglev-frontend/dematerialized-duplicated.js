// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function dematerialized_duplicated(n) {
  let o1 = { x : 42 };
  let o2 = { x : o1, y : o1 };
  if (n < 0) {
    // We won't execute this code when collecting feedback, which means that
    // Maglev will do an unconditional deopt here (and {o2} will thus have no
    // use besides the frame state for this deopt).
    n + 5;
    return o2;
  }
  return n + 2;
}

%PrepareFunctionForOptimization(dematerialized_duplicated);
assertEquals(7, dematerialized_duplicated(5));
assertEquals(7, dematerialized_duplicated(5));

%OptimizeFunctionOnNextCall(dematerialized_duplicated);
assertEquals(7, dematerialized_duplicated(5));

let o = dematerialized_duplicated(-1);
assertSame(o.x, o.y);
assertEquals({ x : { x : 42 }, y : { x : 42 } }, o);

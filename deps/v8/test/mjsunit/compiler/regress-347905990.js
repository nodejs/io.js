// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let c = 0;
function h() { return c++ > 5; }
function g() {}
%NeverOptimizeFunction(g);
%NeverOptimizeFunction(h);

function f(x) {
  // {i} will be a loop phi in the 1st loop, but will also be used in the 2nd
  // one.
  let i = 0;

  for (let j = 0; j < 10; j++, i++) {
    // Calling some non-inlined function to make sure that the loop can't be
    // removed.
    g();
  }

  let v = 0;

  // Now a loop that uses a loop phi in its condition, but it's actually a loop
  // phi from a different loop. And the condition has to be false if the initial
  // value of this loop phi would be used, but since we've already ran the other
  // loop, it's the "final" value of the loop phi that will be used the first
  // time in this loop.
  for (; i > 1; ) {
    if (h()) {
      // This is an opaque way to break out of the loop, which won't be used as
      // the main loop exit condition.
      break;
    }
    v++;
  }

  return v;
}


%PrepareFunctionForOptimization(f);
c = 0;
assertEquals(6, f());

%OptimizeFunctionOnNextCall(f);
c = 0;
assertEquals(6, f());

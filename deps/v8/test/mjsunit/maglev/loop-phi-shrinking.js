// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

// This is a fairly complex tests that tries to combine the following things:
//
//  - compile a function in a garbage-initialized zone (ie, a zone that isn't
//    initialized with 0s).
//
//  - have a loop phi whose input count get reduced (because the graph builder
//    realizes that one of its predecessor is dead)
//
//  - have this loop phi have a Phi as backedge.
//
// A bit of context: in an optimization for Phi untagging, I was iterating Phi
// inputs on RecordUseHint, but I didn't want to visit the backedge if it wasn't
// bound. To know if it was bound or not, I would set it to nullptr when
// creating the loop phi, and I could thus check if the last input was nullptr
// or not. However, this doesn't work when phi inputs get shrunk, as the last
// input (the backedge) could then contain garbage before it's bound (but this
// garbage wouldn't be nullptr, so it would look like a regular valid
// pointer). This didn't show any bug in mjsunit; but this test reproduces the
// error.

function f(a) {
  let x = 2;
  let i = 0;
  if (a) {
    x += 14;
  } else {
    %DeoptimizeNow();
    if (a === 0) {
      // The following addition has no feedback, which will cause Maglev to
      // replace it with an unconditional deopt, thus removing one of the
      // predecessors of the while-loop header.
      x += 25;
    }
  }
  // The following loop has a Phi for `x`, which initially is seen by the graph
  // builder as having 3 predecessors. And its backedge is a Phi (that's
  // important). One of its predecessor is removed during graph building because
  // the `x += 25` is replaced by a Deopt.
  while (i < 5) {
    // We now have an Int32 use of `x`. It should be recorded as such, and
    // should be propagated upwards in the Phi inputs of `x`. However, the
    // backedge hasn't been bound yet, so we should skip it. Note that the
    // backedge is at offset 1 rather than 2 because we've reduced the number
    // of predecessors of `x`.
    x ^ 2;
    x = a ? i - 2 : 8;
    i++;
  }
}

%PrepareFunctionForOptimization(f);
f(1);
%OptimizeMaglevOnNextCall(f);
f(1);
// We now cause `f` to deopt so that it can be re-optimized, ideally using the
// same zone as before (which is thus not 0-initialized anymore but rather
// filled with old garbage in release mode, and filled with non-zero invalid
// pointers in debug mode (something like 0xcdcdcdcdcdcdcdcd).
f();

%OptimizeMaglevOnNextCall(f);
f(1);

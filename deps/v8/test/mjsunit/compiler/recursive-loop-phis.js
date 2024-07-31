// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// This 2 tests contain small loops with mutually recursive loop phis (in each
// case, the second input of `i` and `j` are `j` and `i`). These tests could
// help find bugs in optimization phases that are processing phis "in sequence"
// instead of processing them "in parallel".

// Using "3" as the number of iterations so that the loop could in theory be
// fully unrolled.
function g(a, b) {
  let s = 0;
  for (let c = 0, i = a, j = b; c < 3; [i, j] = [j, i]) {
    s += i + j;
    c++;
  }
  return s;
}

%PrepareFunctionForOptimization(g);
assertEquals(g(8, 3), 33);
%OptimizeFunctionOnNextCall(g);
assertEquals(g(8, 3), 33);


// Using a variable as the number of iterations ("a") so that the loop cannot be
// fully unrolled.
function f(a, b) {
  let s = 0;
  for (let c = 0, i = a, j = b; c < a; [i, j] = [j, i]) {
    s += i + j;
    c++;
  }
  return s;
}

%PrepareFunctionForOptimization(f);
assertEquals(f(8, 3), 88);
%OptimizeFunctionOnNextCall(f);
assertEquals(f(8, 3), 88);

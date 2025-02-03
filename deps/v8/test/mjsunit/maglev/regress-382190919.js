// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-maglev-loop-peeling

function g() { }
%NeverOptimizeFunction(g);

function foo(b) {
  let phi1 = 0;
  for (let i = 0; i < 10; i++) {
    phi1++; // Int32 use so that {phi1} gets untagged.
  }

  let phi2 = undefined; // Not untaggable.
  let j = 0;

  if (b) {
    g(phi1); // Triggering retagging of {phi1}.
  }

  // Nothing between the `if` and the loop header, so that the loop header ends
  // up having 2 incoming forward edges.

  for (; j < 5; j++) {
    phi2 = phi1; // New retagging of {phi1} since previous one is not available
                 // in all predecessors.
  }

  return phi2;
}

%PrepareFunctionForOptimization(foo);
foo(true);
foo(false);

%OptimizeMaglevOnNextCall(foo);
foo(true);

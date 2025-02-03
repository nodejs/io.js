// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-maglev-loop-peeling

// `h` is never inlined, and thus forces a Tagged use on its input.
function h(x) { return x; }
%NeverOptimizeFunction(h);

// `g` creates and returns a Phi that could be untagged.
function g(a) {
  let i = a + 1;
  for (; i < 100; i++) {
    // Padding the bytecode a little bit to make the loop body larger.
    // (basically, we want that the bytecode offset of the call to `h(v)` in `f`
    // be contained this loop here).
    h();
    h();
    h();
    h();
  }
  return i;
}

function f(a) {
  // `v` will be a loop Phis that has an input that itself is a loop phi that is
  // coming from the loop of an inlined function.
  let v = g(a);
  for (let i = 0; i < 10; i++) {
    v = v + 2;
  }
  // We now insert a tagged used of `v`. This should be recorded via
  // RecordUseReprHint, which will record it in the Phi inputs of `v` as well;
  // namely the `g(a)` input. Since this tagged use is not in the `g(a)`'s loop,
  // it should not prevent the phi in `g(a)` from being untagged.
  return h(v);
}

%PrepareFunctionForOptimization(g);
%PrepareFunctionForOptimization(f);
assertEquals(120, f(2));

%OptimizeMaglevOnNextCall(f);
assertEquals(120, f(2));

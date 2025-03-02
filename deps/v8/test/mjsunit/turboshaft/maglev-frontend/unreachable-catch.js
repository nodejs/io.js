// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function f0() {
  for (let i = 0; i < 1; i++) {
    // We're not actually collecting enough feedback for this, which means that
    // Maglev will insert an unconditional Deopt in the loop. However, because
    // this is within a loop, Maglev won't see that the code that follows the
    // loop is actually unreachable. Still, Maglev won't emit the backedge,
    // which means that from Turboshaft's point of vue, we won't have a loop but
    // just a "regular" unconditional Deopt, and Turboshaft thus won't even try
    // to emit anything past this loop.
    class C5 { }
    C5.g = C5;
  }
  try { f0.not_defined(); }
  catch (e) {
    // This catch block is entered when we try to call `f0.not_defined()`.
    // However, because of the unconditional deopt above, it won't be directly
    // reachable from Turboshaft.
    return 42;
  }
  return 17;
}

%PrepareFunctionForOptimization(f0);
assertEquals(42, f0());
%OptimizeFunctionOnNextCall(f0);
assertEquals(42, f0());

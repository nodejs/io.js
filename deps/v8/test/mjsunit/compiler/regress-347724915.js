// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f(...args) {
  let arr1 = [ undefined, undefined, undefined ];
  %SimulateNewspaceFull();
  arr1[0] = args[0];
  // The following allocation will trigger a full GC, which will move the
  // argument passed to the function (because it was a young object).
  let arr2 = [ arr1 ];
  // Here we're accessing `args[0]` again. This might be load-eliminated with
  // the `args[0]` load from a few lines above, which has been moved by the GC
  // since then. This should be fine though, as the loaded value should be
  // marked as Tagged, which means that it shouldn't point to the stale value
  // but instead have been updated during GC.
  arr1[1] = args[0]
  return arr2;
}

%PrepareFunctionForOptimization(f);
let expected = f({ x : 42 });
%OptimizeFunctionOnNextCall(f);
assertEquals(expected, f({x : 42}));

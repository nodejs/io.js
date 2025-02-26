// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev-optimistic-peeled-loops

(function() {
  let i = 0;
  function inc() { i = i + 1; }
  %PrepareFunctionForOptimization(inc);
  while (i < 10) {
    let asdf = 0;
    function xxx() { return asdf; }
    function aliasing() {
      var prevI = i;
      var ok = true;
      for (var y = 0; y < 2; ++y) {
        // Non-peeled outer loop to start with empty analysis state
        for (var x = 1; x < 3; ++x) {
          // Create aliasing cache entry
          inc();
          // Check cache is consistent for every iteration
          if (prevI+x != i) ok = false;
        }
        prevI = i;
      }
      // Assert needs to be outside of loop since it has effects
      assertTrue(ok);
    }
    %PrepareFunctionForOptimization(aliasing);
    aliasing();
    %OptimizeMaglevOnNextCall(aliasing);
  }
})();

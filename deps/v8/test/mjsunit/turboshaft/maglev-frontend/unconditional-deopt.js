// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function deopt(x) {
  if (x) { return 42; }
  else {
    // We won't gather feedback for this during feedback collection, so
    // Maglev will generate an unconditional deopt for this branch.
    return x + 17;
  }
}

%PrepareFunctionForOptimization(deopt);
assertEquals(42, deopt(1));
%OptimizeFunctionOnNextCall(deopt);
assertEquals(42, deopt(1));
assertOptimized(deopt);
assertEquals(17, deopt(0));
assertUnoptimized(deopt);

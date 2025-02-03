// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function round(x, y) {
  let prev_v = y + 14.556;
  let rounded_x = Math.round(x);
  return prev_v + rounded_x;
}

%PrepareFunctionForOptimization(round);
assertEquals(23.116, round(3.25569, 5.56));
%OptimizeFunctionOnNextCall(round);
assertEquals(23.116, round(3.25569, 5.56));
assertOptimized(round);

let o = { valueOf: function() { %DeoptimizeFunction(round); return 4.55 } };

assertEquals(25.116, round(o, 5.56));
assertUnoptimized(round);
assertEquals(25.116, round(o, 5.56));

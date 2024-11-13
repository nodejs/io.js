// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function check_undetectable(x) {
  let r = x == null;
  if (x == null) return 17;
  return r;
};

%PrepareFunctionForOptimization(check_undetectable);
assertEquals(false, check_undetectable(42));
%OptimizeFunctionOnNextCall(check_undetectable);
assertEquals(false, check_undetectable(42));
assertOptimized(check_undetectable);
assertEquals(17, check_undetectable(%GetUndetectable()));
// Should deoptimize because of invalidated NoUndetectableObjects protector.
assertUnoptimized(check_undetectable);
%OptimizeFunctionOnNextCall(check_undetectable);
assertEquals(17, check_undetectable(%GetUndetectable()));
assertOptimized(check_undetectable);

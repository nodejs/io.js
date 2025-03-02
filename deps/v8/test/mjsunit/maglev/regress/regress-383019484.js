// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan --no-use-ic

var v = false;
function f(p) {
  return 0 == (p ? v : null);
}
%PrepareFunctionForOptimization(f);
f(true);

%OptimizeMaglevOnNextCall(f);
assertFalse(f(false));

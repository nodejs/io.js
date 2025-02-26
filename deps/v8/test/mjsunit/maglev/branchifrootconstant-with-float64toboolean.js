// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function f(x) {
  return!!(x * 2) && true;
}

%PrepareFunctionForOptimization(f);
assertFalse(f(0));
assertTrue(f(1.5));

%OptimizeMaglevOnNextCall(f);
assertFalse(f(0));
assertTrue(f(1.5));

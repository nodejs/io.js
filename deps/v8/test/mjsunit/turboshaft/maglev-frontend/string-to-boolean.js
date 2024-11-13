// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f(s) {
  let v = s + "";
  return !!v;
}

%PrepareFunctionForOptimization(f);
assertEquals(true, f("ab"));
assertEquals(false, f(""));
%OptimizeFunctionOnNextCall(f);
assertEquals(true, f("ab"));
assertEquals(false, f(""));
assertOptimized(f);

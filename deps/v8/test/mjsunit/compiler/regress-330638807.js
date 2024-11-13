// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f0() {
  for (const v2 of "charCodeAt") {
    for (let v3 = 0; v3 < 5; v3++) {
      const v4 = ~v3;
      function f5() {
        return v4;
      }
      f5();
      const v9 = (-2147483649n).constructor;
      try { v9.asUintN(1, v9); } catch (e) {}
    }
  }
}

%PrepareFunctionForOptimization(f0);
f0();
f0();

%OptimizeFunctionOnNextCall(f0);
f0();

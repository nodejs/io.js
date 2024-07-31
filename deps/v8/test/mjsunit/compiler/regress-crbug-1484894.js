// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-future

function f0(a2) {
  let v3;
  try { v3 = a2(); } catch (e) {}
  let v4 = 0;
  do {
    for (let i5 = v4; i5 < 2;) {
      i5 / i5 != v3;
      break;
    }
    v4++;
  } while (v4 < 5)
}

%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();

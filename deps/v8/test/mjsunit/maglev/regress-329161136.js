// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function f0() {
  const v2 = [];
  v2[8] = 4.2;
  for (let v4 = 0; v4 < 5; v4++) {
    let v5 = v2[4];
    for (let v6 = 0; v6 < 5; v6++) {
      v4 = v5;
      ++v5;
    }
  }
  return f0;
}

%PrepareFunctionForOptimization(f0);
assertEquals(f0, f0());
assertEquals(f0, f0());
%OptimizeMaglevOnNextCall(f0);
assertEquals(f0, f0());
assertOptimized(f0);

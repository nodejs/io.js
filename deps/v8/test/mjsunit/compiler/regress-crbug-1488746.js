// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const v0 = [-2.0 - 292748.4677736168, -1000.0, -0.34795443553735517 - 450.2748555998029];
function f() {
  for (let v1 = 0; v1 < 10; v1++) {
    for (let i4 = 1, i5 = 10; i5 in v0, i4 < i5; i5--) {
    }
  }
}

%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();

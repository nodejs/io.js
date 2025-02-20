// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

const v2 = new Uint8ClampedArray([1, 2, 3, 4, 5]);
function f3() {
  v2[2] = v2;
}

%PrepareFunctionForOptimization(f3);
f3();
assertEquals(0, v2[2]);

v2[2] = 3;
%OptimizeFunctionOnNextCall(f3);
f3();
assertEquals(0, v2[2]);

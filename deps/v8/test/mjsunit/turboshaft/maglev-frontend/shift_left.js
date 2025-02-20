// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function shift_left(val) {
  // JS spec says that the rhs of a shift should be taken modulo 32. As a
  // result, `1 << 32` is `1 << 0`, which is `1`.
  return 1 << (32 - val);
}

%PrepareFunctionForOptimization(shift_left);
assertEquals(1, shift_left(0));
assertEquals(1, shift_left(0));
%OptimizeFunctionOnNextCall(shift_left);
assertEquals(1, shift_left(0));

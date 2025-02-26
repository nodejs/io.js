// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function ref_err_if_hole(x) {
  switch (x) {
    case 0:
      let v = 17;
    case 1:
      return v;
  }
}

%PrepareFunctionForOptimization(ref_err_if_hole);
assertEquals(17, ref_err_if_hole(0));
%OptimizeFunctionOnNextCall(ref_err_if_hole);
assertEquals(17, ref_err_if_hole(0));

assertThrows(() => ref_err_if_hole(1), ReferenceError,
            "Cannot access 'v' before initialization");
assertOptimized(ref_err_if_hole);

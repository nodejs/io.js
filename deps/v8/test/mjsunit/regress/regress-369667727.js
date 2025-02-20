// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const __v_0 = Uint16Array.toString();
const __v_1 = __v_0.normalize();
function __f_0() {
  const __v_2 = new BigUint64Array(642);
    for (const __v_3 of
    __v_2) {
        ~[-0.6612375122556465, 1.410641326915694e+308, -391827.77944872144, 1000000000.0, -1000.0, 0.0];
    }
  return Math.floor(__v_1 !== __v_0);
}

%PrepareFunctionForOptimization(__f_0);
__f_0();
__f_0();
__f_0();
%OptimizeFunctionOnNextCall(__f_0);
__f_0();
__f_0();

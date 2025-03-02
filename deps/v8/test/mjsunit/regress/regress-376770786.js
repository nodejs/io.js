// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --turbolev

function __f_0(__v_2, __v_3) {
  return __v_2 + __v_3;
}
%PrepareFunctionForOptimization(__f_0);
const __v_0 = 'first';
const __v_1 = new String();
__f_0(__v_0, __v_1);
%OptimizeFunctionOnNextCall(__f_0);
__f_0();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev

let __v_0 = 0;
{
  function __f_0() {
  }
  function __f_2(__v_3) {
      __v_0 = __v_3;
      __v_1 = __v_5;
  }
    %PrepareFunctionForOptimization(__f_2);
  try {
    __f_2(
     2.25);
  } catch (e) {}
  try {
    %OptimizeFunctionOnNextCall(__f_2);
    __f_2();
  } catch (e) {}
  try {
    __f_2( 2.2);
  } catch (e) {}
}

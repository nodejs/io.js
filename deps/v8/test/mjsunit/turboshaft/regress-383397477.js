// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function __f_3() {
  const __v_11 = "//" + Array().join();
  const __v_12 = "'use strict';" + "var x = 23;";
  const __v_13 = __v_12 + "var f = function bozo1() {" + "  return x;" + "};" + "f;" + __v_11;
  assertTrue(
      __v_13.substr("f;") ==
      "'use strict';var x = 23;var f = function bozo1() {  return x;};f;//");
  eval();
}
__f_3();
const __v_9 = %PrepareFunctionForOptimization(__f_3);
const __v_10 = %OptimizeFunctionOnNextCall(__f_3);
__f_3();

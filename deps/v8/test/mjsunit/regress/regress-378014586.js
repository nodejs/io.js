// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function __f_3() {
  return 'aaaaaaaaaaaaaa';
}
function __f_4() {
  return 'bbbbbbbbbbbbbb';
}
function __f_5() {
  return __f_3() + __f_4();
}
function __f_6(__v_9) {
  return __v_9 + __f_5();
}
%PrepareFunctionForOptimization(__f_6);
__f_6("");
%OptimizeFunctionOnNextCall(__f_6);
var __v_8 = __f_6("");
__v_8.replace(/d1/g);
__v_8.replace(/d1/g);

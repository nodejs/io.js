// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function __f_1(__v_191) {
    var __v_17 = 0;
    var __v_18 = __v_191 ? 1 : 2147483648;
    for (var __v_19 = 0; __v_19 < 20; __v_19++) {
        if (__v_191) %OptimizeOsr();
        __v_17 += __v_18;
    }
  return __v_17 + __v_18;
}
%PrepareFunctionForOptimization(__f_1);

function __f_2(__v_20, __v_22) {
    __v_20(true);
    assertEquals(45097156608, __v_20());
}
  __f_2(__f_1);

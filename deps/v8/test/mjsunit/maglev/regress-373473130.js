// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

var __v_52 = {};
function __f_25() {
    if (__v_52 == true) {
      assertTrue(false);
    }
}
%PrepareFunctionForOptimization(__f_25);
__f_25();
%OptimizeFunctionOnNextCall(__f_25);
__f_25();

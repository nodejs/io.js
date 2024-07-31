// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function __f_1(__v_5) {
    var __v_6 = 0;
    var __v_8 = 35;
  try {
    while (__v_8-- > 31) {
        if (__v_6++ == __v_5) try {
        } catch (e) {}
        if (__v_6++ == __v_5) %OptimizeOsr();
    }
  } catch (e) {}
        if (__v_6++ == __v_5) try {
        } catch (e) {}
}
for (var __v_4 = 0; __v_4 < 13; __v_4++) {
 __f_1();
}

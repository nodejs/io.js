// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function get_iterator(o) {
  try {
    [a] = o;
    return a;
  } catch (e) {
    return e;
  }
}

%PrepareFunctionForOptimization(get_iterator);
const error1 = get_iterator(true);
const error2 = get_iterator(42);
const val1 = get_iterator([17]);
%OptimizeFunctionOnNextCall(get_iterator);
assertEquals(error1.message, get_iterator(true).message);
assertEquals(error2.message, get_iterator(42).message);
assertEquals(val1, get_iterator([17]));

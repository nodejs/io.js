// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function make_array() {
  let a = Array();
  assertTrue(%HasSmiOrObjectElements(a));
  return a;
}

%PrepareFunctionForOptimization(make_array);
assertEquals([], make_array());
%OptimizeFunctionOnNextCall(make_array);
assertEquals([], make_array());
assertOptimized(make_array);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function make_array_size(size) {
  let a = Array(size);
  assertTrue(%HasSmiOrObjectElements(a));
  assertTrue(%HasHoleyElements(a));
  assertEquals(size, a.length);
  return a;
}

%PrepareFunctionForOptimization(make_array_size);
assertEquals([,,,,], make_array_size(4));
%OptimizeFunctionOnNextCall(make_array_size);
assertEquals([,,,,], make_array_size(4));
assertOptimized(make_array_size);

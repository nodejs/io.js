// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function for_each_input_fun(f) {
  [1, 2, 3].forEach(f);
}

%PrepareFunctionForOptimization(for_each_input_fun);
assertThrows(() => for_each_input_fun(undefined), TypeError);
assertThrows(() => for_each_input_fun(undefined), TypeError);
%OptimizeFunctionOnNextCall(for_each_input_fun);
assertThrows(() => for_each_input_fun(undefined), TypeError);
assertThrows(() => for_each_input_fun(1), TypeError);
assertThrows(() => for_each_input_fun([]), TypeError);
assertOptimized(for_each_input_fun);
let glob = 0;
assertEquals(undefined, for_each_input_fun(function(v){glob += v}));
assertEquals(glob, 6);
assertOptimized(for_each_input_fun);


function for_each_local_closure(n) {
  let s = 0;
  [1, 2, 3].forEach((v) => s += v + n);
  return s;
}

%PrepareFunctionForOptimization(for_each_local_closure);
assertEquals(15, for_each_local_closure(3));
assertEquals(15, for_each_local_closure(3));
%OptimizeFunctionOnNextCall(for_each_local_closure);
assertEquals(15, for_each_local_closure(3));
assertOptimized(for_each_local_closure);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

let arr = [1, 2, 3, 4, 5, 6];

// Make this slot mutable by assigning it twice. This ensures we are not adding
// a context slot dependency, which will also deoptimize the array_foreach.
// This is not what we want to test here.
let trigger = {};
trigger = false;

function maybe_deopt(v) {
  if (v == 3 && trigger) {
    // The first time this is executed, it will change the map of {arr}, which
    // will trigger a deopt of {array_foreach}.
    arr.x = 11;
  }
  return v;
}

function array_foreach(arr) {
  let s = 0;
  arr.forEach((v) => s += maybe_deopt(v));
  return s;
}


%PrepareFunctionForOptimization(array_foreach);
assertEquals(21, array_foreach(arr));
%OptimizeFunctionOnNextCall(array_foreach);
assertEquals(21, array_foreach(arr));
assertOptimized(array_foreach);

trigger = true;
assertEquals(21, array_foreach(arr));
assertUnoptimized(array_foreach);

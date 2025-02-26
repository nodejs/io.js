// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function alloc_array(n) {
  let a = new Array(n);
  a[0] = 42;
  // not changing a[1]
  a[2] = 17;
  return a;
}

%PrepareFunctionForOptimization(alloc_array);
assertEquals([42, /*hole*/, 17, /*hole*/], alloc_array(3));
%OptimizeFunctionOnNextCall(alloc_array);
assertEquals([42, /*hole*/, 17, /*hole*/], alloc_array(3));
assertOptimized(alloc_array);

// Testing negative length.
assertThrows(() => alloc_array(-2), RangeError);
// Note that this shouldn't deopt the function.
assertOptimized(alloc_array);

// Trying to allocated more than JSArray::kInitialMaxFastElementArray will
// trigger a deopt.
let len_for_deopt = 0x3ffc;
let expected = new Array(len_for_deopt);
expected[0] = 42;
expected[2] = 17;
assertEquals(expected, alloc_array(len_for_deopt));
assertUnoptimized(alloc_array);

%OptimizeFunctionOnNextCall(alloc_array);
assertEquals([42, /*hole*/, 17, /*hole*/], alloc_array(3));
assertOptimized(alloc_array);
// Making sure that we don't deopt anymore when allocating an array of size
// JSArray::kInitialMaxFastElementArray (otherwise, there is a deopt loop).
assertEquals(expected, alloc_array(len_for_deopt));
assertOptimized(alloc_array);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function foo() {
  let val = [];
  val.__proto__ = RegExp();
  return Math.max(...val);
}

%PrepareFunctionForOptimization(foo);
assertThrows(() => foo(), TypeError,
             "Spread syntax requires ...iterable[Symbol.iterator] to be a function");

%OptimizeFunctionOnNextCall(foo);
assertThrows(() => foo(), TypeError,
             "Spread syntax requires ...iterable[Symbol.iterator] to be a function");
assertUnoptimized(foo);


%OptimizeFunctionOnNextCall(foo);
assertThrows(() => foo(), TypeError,
             "Spread syntax requires ...iterable[Symbol.iterator] to be a function");
// TF should not have speculatively optimized CallWithSpread.
assertOptimized(foo);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-always-turbofan --turbofan --js-staging
// Flags: --allow-natives-syntax

let arr = new Float16Array(8);

function f(arr) {
  let it = arr.values();
  let val = it.next();
  return val;
}

%PrepareFunctionForOptimization(f);
let v1 = f(arr);
%OptimizeFunctionOnNextCall(f);
let v2 = f(arr);
assertEquals(v1, v2);
assertOptimized(f);

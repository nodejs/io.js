// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-maglev-loop-peeling
// Flags: --maglev --no-always-turbofan

let obj = { y: 19.5 };
let arr = [, 2.5];
function foo(limit) {
  let val = arr[0];

  for (let i = 0; i < limit; i += 1) {
      i += val;
      val = 40;
  }

  try { val.meh(); } catch (e) {}

  obj.y = val;
}

%PrepareFunctionForOptimization(foo);
foo(1);

%OptimizeMaglevOnNextCall(foo);
foo(0);
// {foo} should have deopted right after being optimized.
assertUnoptimized(foo);
assertEquals(undefined, obj.y);


%OptimizeMaglevOnNextCall(foo);
foo(0);
// {foo} should remain optimized now.
assertOptimized(foo);
assertEquals(undefined, obj.y);

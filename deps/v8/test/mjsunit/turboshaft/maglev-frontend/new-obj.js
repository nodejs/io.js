// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-always-turbofan

function A() { this.x = 42; return 42; }
%NeverOptimizeFunction(A);

function create(c) {
  let x = { "a" : 42, c }; // Creating an object before the Construct call so
                           // that the frame state has more interesting data.
  let y = new A(c);
  return [x, y];
}

%PrepareFunctionForOptimization(create);
create();
let o1 = create();

%OptimizeFunctionOnNextCall(create);
let o2 = create();
assertEquals(o1, o2);
assertOptimized(create);

// Triggering deopt (before the construction) by changing the target.
let new_A_called = false;
A = function() { new_A_called = true; }
assertUnoptimized(create);
%DeoptimizeFunction(create);
let o3 = create();
assertTrue(new_A_called);

// Falling back to generic Construct call.
%OptimizeFunctionOnNextCall(create);
let o4 = create();
assertEquals(o3, o4);
assertOptimized(create);

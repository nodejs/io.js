// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function A(c) {
  if (c) {
    %DeoptimizeFunction(create_deopt);
  }
  this.x = 'abc';
}
%NeverOptimizeFunction(A);

function create_deopt(c) {
  let x = {'a': 42, c};  // Creating an object before the Construct call so
                         // that the frame state has more interesting data.
  let y = new A(c);
  return [x, y];
}

%PrepareFunctionForOptimization(create_deopt);
create_deopt(false);
let o1 = create_deopt(false);

%OptimizeFunctionOnNextCall(create_deopt);
let o2 = create_deopt(false);
assertEquals(o1, o2);
assertOptimized(create_deopt);

// Triggering deopt during the construction
let o3 = create_deopt(true);
assertUnoptimized(create_deopt);
o1[0].c = true; // Fixing {o1} for the comparison, since {o3} was created with
                // `true` as input to `create_deopt`.
assertEquals(o1, o3);

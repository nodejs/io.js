// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function load_field(o) {
  let x = o.x;
  let y = o.y;
  return x + y;
}

let o = { x : 42, y : 15.71 };

%PrepareFunctionForOptimization(load_field);
assertEquals(57.71, load_field(o));
%OptimizeFunctionOnNextCall(load_field);
assertEquals(57.71, load_field(o));
assertOptimized(load_field);

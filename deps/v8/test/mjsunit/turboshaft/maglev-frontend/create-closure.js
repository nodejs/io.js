// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function create_closure(v) {
  let o = {};
  let x = 4;
  o.x = (c) => v + x++ + 2 + c;
  return o;
}

%PrepareFunctionForOptimization(create_closure);
create_closure(7);
%OptimizeFunctionOnNextCall(create_closure);
let o = create_closure(7);
assertEquals(7+4+2+2, o.x(2));
assertEquals(7+5+2+5, o.x(5));
assertOptimized(create_closure);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  let x = 1;
  let y;
  [x] = [, y];
  if (!x ^ y) return 1;
  return 2;
}

%PrepareFunctionForOptimization(foo);
assertEquals(foo(), 1);
assertEquals(foo(), 1);
%OptimizeFunctionOnNextCall(foo);
assertEquals(foo(), 1);

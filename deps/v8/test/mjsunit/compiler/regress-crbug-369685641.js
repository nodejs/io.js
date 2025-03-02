// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-sparkplug --no-maglev

function foo(v1) {
  let v2 = 0;
  switch (v1) {
    case 0:
    case 1:
      v2 = 1;
    case 2:
    case 3:
    case 4:
  }
  return v2;
}

let v = 1;
{
  %PrepareFunctionForOptimization(foo);
  let expected = foo(0);
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(expected, foo(v));
}

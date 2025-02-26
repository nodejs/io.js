// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(o) {
  return o.x;
}

let s1 = new String("abc");
s1.x = 42;

let s2 = new String("def");
s2.y = 17;

%PrepareFunctionForOptimization(foo);
assertEquals(42, foo(s1));

%OptimizeFunctionOnNextCall(foo);
assertEquals(undefined, foo(s2));

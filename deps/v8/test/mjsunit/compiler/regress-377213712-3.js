// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(o) {
  o.x = 42;
}

%PrepareFunctionForOptimization(foo);
let o1 = new String("abc");
foo(o1);
assertEquals(42, o1.x);

let o2 = new String("def");
o2.y = 17;
foo(o2);
assertEquals(42, o2.x);

%OptimizeFunctionOnNextCall(foo);
let o3 = new String("ghi");
foo(o3);
assertEquals(42, o3.x);

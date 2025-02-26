// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function foo(o) {
  return "x" in o;
}

%PrepareFunctionForOptimization(foo);
let o1 = new String("abc");
assertEquals(false, foo(o1));

let o2 = new String("def");
o2.x = 17;
assertEquals(true, foo(o2));
print(foo(o2));

%OptimizeFunctionOnNextCall(foo);
let o3 = new String("ghi");
assertEquals(false, foo(o3));
o3.x = 42;
assertEquals(true, foo(o3));

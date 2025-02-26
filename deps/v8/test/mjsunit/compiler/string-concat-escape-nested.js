// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis

function foo(s1, s2, c) {
  let str1 = s1 + s2;
  let o = { x : str1, y : 42 };
  if (c) {
    return o.x;
  }
  return o.y;
}

%PrepareFunctionForOptimization(foo);
assertEquals(42, foo("hello", " world!", false));
assertEquals(42, foo("hello", " world!", false));

%OptimizeFunctionOnNextCall(foo);
assertEquals(42, foo("hello", " world!", false));
assertEquals("hello world!", foo("hello", " world!", true));

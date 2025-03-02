// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function foo(s) {
  return s[5];
}

%PrepareFunctionForOptimization(foo);
assertEquals("f", foo("abcdef"));
assertEquals(undefined, foo("a"));
%OptimizeFunctionOnNextCall(foo);
assertEquals("f", foo("abcdef"));
assertEquals(undefined, foo("a"));
assertOptimized(foo);

// Now mess with the String.prototype.
String.prototype[5] = "5";

assertUnoptimized(foo);
%DeoptimizeFunction(foo);

assertEquals("f", foo("abcdef"));
%PrepareFunctionForOptimization(foo);
assertEquals("5", foo("a"));
%OptimizeFunctionOnNextCall(foo);
assertEquals("f", foo("abcdef"));
assertEquals("5", foo("a"));
assertOptimized(foo);

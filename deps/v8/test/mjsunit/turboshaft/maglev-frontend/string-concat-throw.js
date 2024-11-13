// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

var a = "a".repeat(%StringMaxLength());

function foo(a, b) {
  try {
    return a + "0123";
  } catch (e) {
    return e;
  }
}

%PrepareFunctionForOptimization(foo);
assertEquals("a0123", foo("a"));
assertEquals("a0123", foo("a"));

%OptimizeFunctionOnNextCall(foo);
assertEquals("a0123", foo("a"));
assertOptimized(foo);

// Triggering a thow by creating a too-large string. This will deopt the
// function since this is the 1st time that it throws.
assertInstanceof(foo(a), RangeError);
assertUnoptimized(foo);

// Reoptimizing the function. This time, it should deopt after throwing.
%OptimizeFunctionOnNextCall(foo);
assertEquals("a0123", foo("a"));
assertInstanceof(foo(a), RangeError);
assertOptimized(foo);

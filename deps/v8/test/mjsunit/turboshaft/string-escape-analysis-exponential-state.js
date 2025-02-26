// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis
// Flags: --turbofan --no-always-turbofan


function foo(str, c) {
  // Without de-duplication of the string concatenations in the FrameState, the
  // size of the FrameState will grow exponentially.
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  str += str;
  if (c) {
    // We're not collecting feedback for this, which means that it will be an
    // unconditional Deopt with `str` as input in the FrameState.
    return str + str;
  }
}


%PrepareFunctionForOptimization(foo);
foo("a", false);
foo("a", false);

%OptimizeFunctionOnNextCall(foo);
foo("a", false);
assertOptimized(foo);

// We pass `true` to the function, which triggers a deopt and causes it to
// return the string.
assertEquals("a".repeat(2 ** 21), foo("a", true));
assertUnoptimized(foo);

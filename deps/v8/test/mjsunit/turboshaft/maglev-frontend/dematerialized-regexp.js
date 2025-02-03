// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan
// Flags: --no-always-turbofan

function bar(re) {
  let match1 = re.test("123Art");
  let match2 = re.test("b123ze");
  let match3 = re.test("C");
  let nomatch1 = re.test("1234556");
  let nomatch2 = re.test("iopdsd4 ,!:^");
  return match1 && match2 && match3 && !nomatch1 && !nomatch2;
}
%NeverOptimizeFunction(bar);

function foo(b) {
  let re = /[abc]/i;
  if (b) {
    // We're not collecting feedback for this case. We'll just have an
    // unconditional deopt here, and {re} will be escape-analyzed away and will
    // flow in the FrameState here. Since RegExp data are in trusted space,
    // the FrameState will thus have a trusted object as input.
    return bar(re);
  }
  return 42;
}

%PrepareFunctionForOptimization(foo);
assertEquals(42, foo(false));
assertEquals(42, foo(false));
%OptimizeFunctionOnNextCall(foo);
assertEquals(42, foo(false));
assertOptimized(foo);

// Triggering a deopt, which will materialize the regex object.
assertEquals(true, foo(true));
assertUnoptimized(foo);

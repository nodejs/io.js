// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis
// Flags: --turbofan

// In this test, `bar` will have a FrameState that contains an escaped string
// concatenation, but it will actually be deduplicated to a reference to the
// same string concatenation in its parent FrameState (for foo).

function bar(a, b) {
  %DeoptimizeFunction(foo);
}

function foo(a) {
  bar('@@' + a + '~~');
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo('a');
foo('a');

%OptimizeFunctionOnNextCall(foo);
foo('a');

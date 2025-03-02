// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-string-concat-escape-analysis
// Flags: --turbofan --no-always-turbofan

// This test ensures that string concat escape analysis does not try to create a
// FrameState with more inputs that the maximum number of inputs allowed in
// Turboshaft (2**16-1).

// The FrameState for `return s12 + a` will contain:
//   * The Closure => 1 input
//   * Parameters:
//     * this => 1 input
//     * "should_return" and "a" => 2 inputs
//     * and other parameters
//   * Context => 1 input
//   * Local:
//     * s12 => 1 input
//   * Accumulator: a => 1 input
//
// So, that's 7 "fixed" input + all of the additional parameters. Then, string
// concat escape analysis should add 12 parameters (s1, s2, ..., s12). The
// maximum input count for a Turboshaft node is 2**16-1. So, we need to add n
// additional parameters such that n+12+7 is greater than 2**16-1, so we need at
// least 2**16-1-12 additional parameters. And we'll add a "-3" to this number,
// just to have a small buffer, to make the code instruction selector and
// generator happy (details, details, details, don't ask).
let additional_input_count = 2**16-1-12-3;
let fun_str = 'function foo(should_return, a';
for (let i = 0; i < additional_input_count; i++) {
  fun_str += `, v${i}`;
}
fun_str += `) {
  let s1 = a + a;
  let s2 = s1 + a;
  let s3 = s2 + a;
  let s4 = s3 + a;
  let s5 = s4 + a;
  let s6 = s5 + a;
  let s7 = s6 + a;
  let s8 = s7 + a;
  let s9 = s8 + a;
  let s10 = s9 + a;
  let s11 = s10 + a;
  let s12 = s11 + a;
  if (should_return) {
    // We won't record feedback for this, which means that
    // we'll have an unconditional deopt here.
    return s12 + a;
  }
}`;

eval(fun_str);

%PrepareFunctionForOptimization(foo);
foo(false, "a");
foo(false, "a");

%OptimizeFunctionOnNextCall(foo);
foo(false, "a");
assertOptimized(foo);

// Passing `true` to `foo`, which will cause it to return the string (and,
// deopt, since we didn't record feedback for this.
assertEquals("aaaaaaaaaaaaaa", foo(true, "a"));

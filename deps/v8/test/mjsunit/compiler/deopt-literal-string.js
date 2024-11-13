// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// Flags: --allow-natives-syntax --expose-gc

function add(s1, s2) { return s1 + s2; }

function f(x) {
  let str = add("abcabcabcabcabc", "defdefdefdefdefdef");
  // After inlining of {add}, {str} will be constant-folded.
  let o = [ str, x ];

  // {o} will be escape-analyzed away, and {str} will flow into a FrameState and
  // thus be recorded in the DeoptimizationLiteralArray of the function. This
  // will be the only pointer to {str}, and should thus not be weak so
  // that {str} isn't freed on GC.

  return o[1] + str.length;
}

%PrepareFunctionForOptimization(add);
%PrepareFunctionForOptimization(f);
assertEquals(75, f(42));
%OptimizeFunctionOnNextCall(f);
assertEquals(75, f(42));

gc();
gc();

// And now trigger the deopt.
assertEquals("abc33", f("abc", true));

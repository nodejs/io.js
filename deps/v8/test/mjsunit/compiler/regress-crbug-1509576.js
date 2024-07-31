// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function escape(s) { }

function f(i) {
  let str = "";
  escape(str);

  // This "if (i == 3)" should not be merged into the subsequent switch, because
  // there is a side-effect in between.
  if (i == 3) {
    // This will trigger a deopt
    str += "("
  }

  str += "function";

  switch (i) {
    case -10:
      escape(str);
    case 1:
    case 3:
  }

  // This `eval` creates some kind of closure of the function inside the
  // function, not sure how that works exactly, but it's needed to repro :D
  eval();

  return str;
}

%PrepareFunctionForOptimization(f);
assertEquals(f(0), "function");
%OptimizeFunctionOnNextCall(f);
assertEquals(f(3), "(function");

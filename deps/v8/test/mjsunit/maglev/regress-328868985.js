// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-inlining
// Flags: --maglev-escape-analysis

function bar() {}

function foo(b) {
  const a = [4.2];  // Object id = 0,
                    // double elements array is object id = 1
  function inner() {
    const o1 = {}    // Object id = 0 (again)
    const o2 = {}    // Object id = 1 (again)
    if (b) {
      bar();         // Deopt
    }
    return o2.d; // it should be undefined
                 // but if materialization is wrong, it can
                 // crash or return something else.
  }
  return inner(a);
}

%PrepareFunctionForOptimization(foo);
foo(false);
foo(false);
%OptimizeMaglevOnNextCall(foo);
foo(true);

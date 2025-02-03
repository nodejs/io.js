// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function bar(b) {
  if (b) {
    throw_before_this_function_is_not_defined();
  }
}

function foo(a, b) {
  let r = a >>> b;
  try {
    bar();
    r = 45;
    const o = {
      get g() {
        return r;
      },
    };
    bar(true);
  } catch(e) {
    return r + 1;
  }
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeFunctionOnNextCall(foo);
foo();

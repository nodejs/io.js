// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function() {
  const bar = {};
  function foo(a) {
    a === a;
    try {
      a = 1;
      bar();
      arguments;
    } catch {}
  }

  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
}());

(function() {
  function bar() {}

  const o = {};
  function foo(x) {
    bar();
    let args = arguments;
    ({"x":x,"y":y,...args} = o);
  }

  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
}());

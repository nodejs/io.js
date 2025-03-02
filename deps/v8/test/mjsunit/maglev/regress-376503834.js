// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo() {
  const o2 = { };
  function bar(a4) {
    return o2[a4];
  }
  %PrepareFunctionForOptimization(bar);
  bar("c" + "c");

  %OptimizeMaglevOnNextCall(bar);
  bar("c" + "c");
  bar();
}

%PrepareFunctionForOptimization(foo);
foo();

%OptimizeMaglevOnNextCall(foo);
foo();

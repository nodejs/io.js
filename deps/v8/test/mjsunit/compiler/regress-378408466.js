// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function bar(x) {
  const len = x.length;
  return len ^ 2;
}

const Constr = {}.constructor;

function foo() {
  const obj = Constr("getOwnPropertyDescriptors");

  bar("p");
  bar(obj);
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
foo();

%OptimizeFunctionOnNextCall(foo);
foo();

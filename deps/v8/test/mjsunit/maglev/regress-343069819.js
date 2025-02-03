// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function bar() {
  const v4 = [-157755.311127503641e-15,-1000000000000.0];
  let v5 = -10;
  v5--;
  v4.a = v5;
  return v5;
}
Symbol[Symbol.iterator] = bar;
function foo() {
  try { new foo(undefined, ...Symbol, undefined); } catch (e) {}
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function bar(a, b) {
  return arguments;
}

function foo(x) {
  return bar(x)[0] == 42;
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo();
%OptimizeMaglevOnNextCall(foo);
assertFalse(foo(24));
assertTrue(foo(42));

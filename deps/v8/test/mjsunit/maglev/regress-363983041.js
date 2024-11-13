// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(x) {
  return new x();
}

%PrepareFunctionForOptimization(foo);
foo(Object);
%OptimizeFunctionOnNextCall(foo);
assertThrows(foo, TypeError);

// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging

function foo() {
  var arr = new Float16Array(1);
  arr[0] = undefined;
  return arr[0];
}

%PrepareFunctionForOptimization(foo)
foo();
%OptimizeFunctionOnNextCall(foo);
assertTrue(isNaN(foo()));

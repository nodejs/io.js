// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo(n, x) {
  for (var i = 0; i < n; i++) {
    x = 1 << 31 - i;
  }
}
%PrepareFunctionForOptimization(foo);
foo(1, 1);
%OptimizeFunctionOnNextCall(foo);
foo();

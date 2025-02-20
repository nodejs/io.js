// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let slot = 0;
function foo(a) {
  slot = a;
}
%PrepareFunctionForOptimization(foo);
foo(0x4fffffff);
%OptimizeFunctionOnNextCall(foo);
foo();

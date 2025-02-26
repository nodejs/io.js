// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --turboshaft-typed-optimizations

function foo() {
  return [5.35, /* hole */, 3.35];
}

%PrepareFunctionForOptimization(foo);
assertEquals([5.35,,3.35], foo());

%OptimizeFunctionOnNextCall(foo);
assertEquals([5.35,,3.35], foo());

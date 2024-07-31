// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: -allow-natives-syntax --turboshaft --turboshaft-assert-types

function f() {
  for (let i = 1; i < 100; i *= 4) {
  }
}

%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();

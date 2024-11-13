// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f7() {
  for (let i10 = -2147483648; i10 < 268435441; i10 += 100000) {
  }
}

%PrepareFunctionForOptimization(f7);
f7();
%OptimizeFunctionOnNextCall(f7);
f7();

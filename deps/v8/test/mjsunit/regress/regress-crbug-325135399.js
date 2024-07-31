// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const v2 = 0;

if (true) {
 function f35() {
    const v39 = true;
    [,...o36] = "p";
    v39["p"] = v2;
    eval();
  }
  %PrepareFunctionForOptimization(f35);
  f35();
  %OptimizeFunctionOnNextCall(f35);
  f35();
}

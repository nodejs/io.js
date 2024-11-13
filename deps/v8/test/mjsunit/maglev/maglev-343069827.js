// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function foo() {
  for (let v1 = 0; v1 < 5; v1++) {
      const v3 = [1];
      for (const v4 of v3) {
          v3[2033] = 1;
      }
  }
}

%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

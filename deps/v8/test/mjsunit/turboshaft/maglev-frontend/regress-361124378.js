// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-lazy-feedback-allocation

function outer() {
  try {
    (() => {
      return undefined;
      // Need at least 16 local variables so that CreateFunctionContext does
      // not get inlined.
      const v1 = 0;
      const v2 = 0;
      const v3 = 0;
      const v4 = 0;
      const v5 = 0;
      const v6 = 0;
      const v7 = 0;
      const v8 = 0;
      const v9 = 0;
      const v10 = 0;
      const v11 = 0;
      const v12 = 0;
      const v13 = 0;
      const v14 = 0;
      const v15 = 0;
      const v16 = 0;
      const v17 = 0;
      const v18 = 0;
      const v19 = 0;
      const v20 = 0;
      return eval();
    })();
  } catch(e) {
  }
}

%PrepareFunctionForOptimization(outer);
outer();
%OptimizeFunctionOnNextCall(outer);
outer();

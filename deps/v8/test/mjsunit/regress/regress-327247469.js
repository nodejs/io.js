// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbofan --no-always-turbofan
// Flags: --const-tracking-let --no-lazy-feedback-allocation

let v15 = 0;
const v22 = [];
let v23 = -1;
function f25(a) {
  (() => {
      v23--;
      function C(b) {}
      new C(a);
  })();
}
%PrepareFunctionForOptimization(f25);
f25({});
%OptimizeFunctionOnNextCall(f25);
f25({});

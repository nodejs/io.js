// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

function f_eval() {
  let i = 0.1;
  eval();
  if (i) {
    const c = {};
    eval();
  }
}

%PrepareFunctionForOptimization(f_eval);
f_eval();
f_eval();
%OptimizeFunctionOnNextCall(f_eval);
f_eval();
assertOptimized(f_eval);

// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft --allow-natives-syntax --jit-fuzzing

const v1 = {};

function f2() {
  for (let i4 = 1;
       i4 > -1;
       (() => {
         const v10 = this.version;
         v10.h = v10;
         i4 = i4 - 2;
       })()) {
         v1.c &= i4;
       }
}

%PrepareFunctionForOptimization(f2);
f2();
%OptimizeFunctionOnNextCall(f2);
f2();

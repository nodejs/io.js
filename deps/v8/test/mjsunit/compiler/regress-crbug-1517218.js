// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-assert-types

function f() {
  function asmModule() {
    'use asm';
    function x(v) {
      v = v | 0;
    }
    return x;
  }
  const asm = asmModule();
  asm();
}

%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();

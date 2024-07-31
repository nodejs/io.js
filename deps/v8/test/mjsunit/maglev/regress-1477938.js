// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --no-maglev-inlining

function foo() {
  return undefined;
}

function opt(){
  let a = 4096;
  let b = 13;
  for (let i = 0; i < 10; i++) {
    try {
      let f = foo()
      ++b;
      let c = '' ** b;
      a = i >>> c;
      f();
    } catch {
    }
  }
  return a;
}

%PrepareFunctionForOptimization(opt);
assertEquals(9, opt());
%OptimizeMaglevOnNextCall(opt);
assertEquals(9, opt());

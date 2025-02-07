// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  switch ([]) {
    default:
      const v12 = typeof 10;
      const v15 = v12 === "object" > v12;
      [-65536].indexOf( ...arguments);
      break;
    case {}:
      for (let i = 0; i < 5; i++) {
        function bar() { eval(); }
      }
  }
}
%PrepareFunctionForOptimization(foo);
foo();
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

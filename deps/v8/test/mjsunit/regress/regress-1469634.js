// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function Module(stdlib) {
  "use asm";

  var fround = stdlib.Math.fround;

  // f: double -> float
  function f(a) {
    a = +a;
    return fround(a);
  }

  return { f: f };
}

var f = Module({ Math }).f;
let count = 0;

let tester = () => {
    return f(140737463189505, 8388607);
}
%PrepareFunctionForOptimization(tester);
tester();
%OptimizeFunctionOnNextCall(tester);
tester();

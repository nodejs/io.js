
// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function foo() {
  for (let v0 = 0; v0 < 5; v0++) {
    const v1 = v0 >> v0;
    const v4 = new Int16Array(1156);
    const v6 = [];
    const v7 = [];
    try { Uint16Array(...v7, ...v6, ...v4); } catch (e) {}
    function f9() {
        return arguments;
    }
    const v12 = f9();
    function f13(a14, a15) {
        a15[3] = a15;
        undefined instanceof a14;
    }
    try { f13(v1); } catch (e) {}
    f13(Int16Array, v12);
    %OptimizeOsr();
  }
}

%PrepareFunctionForOptimization(foo);
foo();

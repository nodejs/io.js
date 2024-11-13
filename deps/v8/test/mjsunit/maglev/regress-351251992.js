// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function () {
  function foo() {
    var __v_54 = 0;
    __v_54++;
    String.prototype[Symbol.iterator].call(__v_54);
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

(function () {
  function foo() {
    const v3 = new Int16Array(16);
    for (let v4 of v3) {
      v4--;
      ("getDate")[Symbol.iterator].call(v4);
    }
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

(function () {
  let v1 = 3 + 3;
  class C2 {
      constructor() {
          for (let i = 0; i < 5; i++) {
              const v8 = 8 + v1--;
              const v9 = v8 * v8;
              try {
                  super.o();
              } catch(e11) {
                  function f12(a13) {
                      a13.stack;
                  }
                  f12(e11);
                  f12(v9);
              }
          }
      }
  }
  %PrepareFunctionForOptimization(C2);
  new C2();
  %OptimizeFunctionOnNextCall(C2);
  new C2();
})();

(function () {
  function foo() {
    return -Infinity !== null;
  }

  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

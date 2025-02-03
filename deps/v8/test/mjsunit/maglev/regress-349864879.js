// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function() {
  function f0() {
    const v3 = ([-639548.3633151249,-1e-15]).with();
    const v4 = ("NffG").charCodeAt(f0);
    const v5 = v4 - v4;
    function f6(a7, a8, a9) {
        try { a7.reduce(); } catch (e) {}
        return a7;
    }
    %PrepareFunctionForOptimization(f6);
    f6(v5);
    f6(v3);
    return f0;
  }
  %PrepareFunctionForOptimization(f0);
  const v13 = f0();
  v13.call(v13, v13);
  %OptimizeMaglevOnNextCall(f0);
  f0();
})();

(function() {
  function foo(v1) {
    const v2 = v1 % v1;
    function f3(a4) {
        a4.x;
    }
    %PrepareFunctionForOptimization(f3);
    f3(v2);
    f3(-9007199254740990n);
  }
  %PrepareFunctionForOptimization(foo);
  foo(0);
  foo(1);
  %OptimizeMaglevOnNextCall(foo);
  foo(2);
})();

(function() {
  const v1 = new Uint16Array(Uint16Array, Uint16Array, Uint16Array);
  function f2() {
      const o4 = {
          "d": 1563771623,
      };
      return o4;
  }
  const v5 = f2();
  function f6() {
      function f7(a8, a9, a10, a11) {
          a8.d.toString();
          return a8;
      }
      %PrepareFunctionForOptimization(f7);
      f7(v5);
      f7(f6);
      return f2;
  }
  function f16() {
      return v1;
  }
  Object.defineProperty(f6, "d", { get: f16 });
  %PrepareFunctionForOptimization(f6);
  f6();
  f6();
  %OptimizeMaglevOnNextCall(f6);
  f6();
})();

(function() {
  %PrepareFunctionForOptimization(f3);
  %PrepareFunctionForOptimization(foo);
  function F0() {}
  const v2 = new F0();
  function f3(a4) {
    return a4.b;
  }
  f3(v2);
  const v10 = new Float64Array(256);
  function foo(v11) {
    const v12 = v11.substring();
    const v13 = Math.min();
    const v14 = v13 - v13;
    v12 > v10;
    f3(v14);
  }
  foo("42");
  foo("42");
  %OptimizeMaglevOnNextCall(foo);
  foo("42");
})();

(function() {
  const v2 = [1.1,,];
  function f3(a4) {
      const v5 = v2[a4];
      if (v2[a4] === undefined) {
          return 1;
      }
      v5.toString();
      return undefined;
  }
  %PrepareFunctionForOptimization(f3);
  f3(0);
  f3(1);
  %OptimizeMaglevOnNextCall(f3);
  f3(v2);
})();

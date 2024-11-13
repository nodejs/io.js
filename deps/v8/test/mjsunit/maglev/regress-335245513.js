// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

// Consider dead branches.
(function() {
  function bar(x) { o.x; }
  function foo(x) {
    // We create a diamond shape with 2 calls to bar. One normal for
    // null/undefineds x's and one with CallWithArryLike. The first one
    // will be inlined and its compilation will abort due to lack of
    // feedback in the accessor.
    try { bar.apply(this, x); } catch {}
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

// Consider untagged values passed to CallWithArray.
(function() {
  function bar() {}
  function foo(x) {
    x++; // We guarantee that the x is a Float64 node.
    try { bar.apply(this, x); } catch {}
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

// Consider untagged values passed to the branch node.
(function() {
  let a = [4.2, /*hole*/, 4.2];
  function bar() {}
  function foo(i) {
    let x = a[i];
    try { bar.apply(this, x); } catch {}
  }
  %PrepareFunctionForOptimization(foo);
  foo(a[1]);
  foo(a[1]);
  %OptimizeFunctionOnNextCall(foo);
  foo(a[1]);
})();

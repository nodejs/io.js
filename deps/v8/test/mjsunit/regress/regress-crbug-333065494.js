// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --stress-concurrent-inlining-attach-code
// Flags: --stress-concurrent-inlining --no-maglev-overwrite-budget
// Flags: --no-assert-types

function foo(__v_56, v) {
  var bla = {
    x: v,
    __proto__: __v_56
  };
  bla.x;
}
%PrepareFunctionForOptimization(foo);
(function() {
  foo(arguments, 0);
})();
(function() {
  foo(arguments, 0);
  // Deprecate bla's map.
  foo(arguments, NaN);
})();
// Optimize function with deprecated map in IC
%OptimizeFunctionOnNextCall(foo);
foo();

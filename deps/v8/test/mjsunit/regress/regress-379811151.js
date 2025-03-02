// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --expose-gc

(function () {
  __callGC = function () {
    gc();
  };
})();
(function () {
  function __f_85() {
  }
  function __f_86() {
  }
  %PrepareFunctionForOptimization(__f_86);
  %OptimizeFunctionOnNextCall(__f_86);
  __callGC();
  __f_85(), __f_86();
})();

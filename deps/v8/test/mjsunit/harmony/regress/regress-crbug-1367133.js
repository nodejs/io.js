// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --stress-concurrent-inlining

(function TestArray() {
  function doCall(a, method, ...args) { a[method](); }
  function callOnArray(a) { doCall(a, 'with'); a.keys(); }

  %PrepareFunctionForOptimization(callOnArray);
  callOnArray([1]);
  doCall({}, 'valueOf', "foo");
  %OptimizeFunctionOnNextCall(callOnArray);
  callOnArray([{},]);
})();

(function TestTypedArray() {
  function doCall(a, method, ...args) { a[method](); }
  function callOnArray(a) { doCall(a, 'with'); a.keys(); }

  %PrepareFunctionForOptimization(callOnArray);
  callOnArray(new Uint8Array(32));
  doCall({}, 'valueOf', "foo");
  %OptimizeFunctionOnNextCall(callOnArray);
  callOnArray(new Float64Array(8));
})();

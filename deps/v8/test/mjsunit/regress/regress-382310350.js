// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --use-std-math-pow

function main(){
  var v0 = new Int32Array([2158557328]);
  return (v0[0] ** 2) - 4564247551369761000;
}

%PrepareFunctionForOptimization(main);
assertEquals(0, main());
%OptimizeFunctionOnNextCall(main);
assertEquals(0, main());

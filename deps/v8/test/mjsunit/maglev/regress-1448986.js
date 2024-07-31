// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

let count = 0;
Array.prototype[Symbol.iterator] = function () {
  count++;
  return {next() { return {done:true} }}
};
class c_1 {}
class c_2 extends c_1 {}
%PrepareFunctionForOptimization(c_2);
%OptimizeMaglevOnNextCall(c_2);
new c_2();
assertEquals(0, count);

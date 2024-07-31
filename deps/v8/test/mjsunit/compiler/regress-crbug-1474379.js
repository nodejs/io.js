// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-maglev

let d8 = this.d8;
const obj = Object();

function foo() {
  return Promise.resolve(obj);
}

%PrepareFunctionForOptimization(foo);
assertNotEquals(foo(), undefined);

Object.defineProperty(obj, "then", {
  get: function () { d8.debugger.enable(); },
});

%OptimizeFunctionOnNextCall(foo);
assertNotEquals(foo(), undefined);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

function callFun(fun) {
    fun();
}
var iterable = {};
iterable[Symbol.iterator] = () => ({
  next: () => ({}),
  return: () => {
  }
});
function* iterateAndThrow() {
  for (let x of iterable) {
    throw 42;
  }
}
%PrepareFunctionForOptimization(iterateAndThrow);

try {
  callFun(() => iterateAndThrow().next());
} catch (e) {}

iterateAndThrow();
%OptimizeMaglevOnNextCall(iterateAndThrow);

try {
  iterateAndThrow().next();
} catch {
}

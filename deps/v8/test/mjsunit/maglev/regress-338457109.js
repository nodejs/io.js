// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function probe(arg) {
  var retThis = function () {
    return this;
  };
  let a = retThis.call(arg);
  let b = retThis.call(arg);
  assertFalse(a===b);
}
var args = [ Symbol.for(), Symbol.iterator];
%PrepareFunctionForOptimization(probe);
for (var arg of args) {
    probe(arg);
    %OptimizeMaglevOnNextCall(probe);
}

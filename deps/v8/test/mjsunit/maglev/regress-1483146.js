// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev --maglev-inlining

let result;

function dontOpt(x) {
  result = x;
}

(function () {
  prettyPrinted = function prettyPrinted(value) {
    return value;
  }
  let maxExtraPrinting = 100;
  prettyPrint = function (value, extra = false) {
    let str = prettyPrinted(value);
    if (extra && maxExtraPrinting-- <= 0) {
      return;
    }
    dontOpt(str);
  };
  printExtra = function (value) {
    prettyPrint(value, true);
  };
})();

function empty() {}
let g;
var foo = function () {
  printExtra();
  g =  empty();
  printExtra((this instanceof Object));
};

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(printExtra);
%PrepareFunctionForOptimization(prettyPrint);
%PrepareFunctionForOptimization(prettyPrinted);
%PrepareFunctionForOptimization(empty);
foo();
assertEquals(true, result);
%OptimizeMaglevOnNextCall(foo);
foo();
assertEquals(true, result);

// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is executed separately before the correctness test case. Add here
// checking of global properties that should never differ in any configuration.
// A difference found in the prints below will prevent any further correctness
// comparison for the selected configurations to avoid flooding bugs.

print("https://crbug.com/932656");
print(Object.getOwnPropertyNames(this));

print("https://crbug.com/935800");
(function () {
  function foo() {
    "use asm";
    function baz() {}
    return {bar: baz};
  }
  print(Object.getOwnPropertyNames(foo().bar));
})();

print("https://crbug.com/985154");
(function () {
  "use strict";
  function foo() {
    "use asm";
    function baz() {}
    return {bar: baz};
  }
  print(Object.getOwnPropertyNames(foo().bar));
})();

print("Sensitive runtime functions are neutered");
(function () {
  function foo() {}
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
  print(%GetOptimizationStatus(foo));
  const fun = new Function("f", "return %GetOptimizationStatus(f);");
  print(fun(foo));
})();

// Don't change this line, it's used to separate smoketest output.
print("___foozzie___smoke_test_end___");

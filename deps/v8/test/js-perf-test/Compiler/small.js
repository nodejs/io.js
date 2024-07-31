// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(() => {

  let a = 2, b = 3;
  let arr = [1, 2, 3, 4, 5];

  function empty() {}

  function constant() {
    return 42;
  }

  function add() {
    return a + b;
  }

  function load() {
    return arr[2];
  }

  // Initializing feedback for each function
  %PrepareFunctionForOptimization(empty);
  empty();

  %PrepareFunctionForOptimization(constant);
  constant();

  %PrepareFunctionForOptimization(add);
  add();
  a = 4; b = 4;
  add();

  %PrepareFunctionForOptimization(load);
  load();
  arr[2] = 17;
  load();


  // Creating runners
  function run_empty() {
    %BenchTurbofan(empty, 100);
  }
  function run_constant() {
    %BenchTurbofan(constant, 100);
  }
  function run_add() {
    %BenchTurbofan(add, 100);
  }
  function run_load() {
    %BenchTurbofan(load, 100);
  }

  // Registering tests
  createSuite('Small-Empty', 1, run_empty);
  createSuite('Small-Constant', 1, run_constant);
  createSuite('Small-Add', 1, run_add);
  createSuite('Small-Load', 1, run_load);
})();

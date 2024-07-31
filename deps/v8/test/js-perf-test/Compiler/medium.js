// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(() => {

  let a = 19, b = 3;
  let i = {};

  function copy() {
    let o = {};
    o.a0 = i.a0;
    o.a1 = i.a1;
    o.a2 = i.a2;
    o.a3 = i.a3;
    o.a4 = i.a4;
    o.a5 = i.a5;
    o.a6 = i.a6;
    o.a7 = i.a7;
    o.a8 = i.a8;
    o.a9 = i.a9;
    o.a10 = i.a10;
    o.a11 = i.a11;
    o.a12 = i.a12;
    o.a13 = i.a13;
    o.a14 = i.a14;
    o.a15 = i.a15;
    o.a16 = i.a16;
    o.a17 = i.a11;
    o.a18 = i.a18;
    o.a19 = i.a19;
    return o;
  }

  function fact() {
    let i = a;
    let r = 1;
    while (i != 0) {
      r *= i;
      i--;
    }
    return i;
  }

  function is_prime() {
    for (let i = 2; i <= Math.sqrt(a); i++) {
      if (a % i == 0) return false;
    }
    return true;
  }

  function eratosthenes(max) {
    let nums = new Array(max).fill(false);
    nums[0] = nums[1] = true;
    for (let i = 2; i <= Math.sqrt(max); i++) {
      if (nums[i]) continue;
      for (var j = i * i; j < max; j += i) {
        nums[j] = true;
      }
    }
    return nums;
  }

  // Initializing feedback for each function
  %PrepareFunctionForOptimization(copy);
  i = copy();
  copy();

  %PrepareFunctionForOptimization(fact);
  fact();
  a = 29;
  fact();

  %PrepareFunctionForOptimization(is_prime);
  is_prime();
  a = 23;
  is_prime();
  a = 25;
  is_prime();

  %PrepareFunctionForOptimization(eratosthenes);
  eratosthenes(1000);

  // Creating runners
  function run_copy() {
    %BenchTurbofan(copy, 10);
  }
  function run_fact() {
    %BenchTurbofan(fact, 10);
  }
  function run_prime() {
    %BenchTurbofan(is_prime, 10);
  }
  function run_eratosthenes() {
    %BenchTurbofan(eratosthenes, 10);
  }

  // Registering tests
  createSuite('Medium-Copy', 1, run_copy);
  createSuite('Medium-Fact', 1, run_fact);
  createSuite('Medium-Prime', 1, run_prime);
  createSuite('Medium-Eratosthenes', 1, run_eratosthenes);
})();

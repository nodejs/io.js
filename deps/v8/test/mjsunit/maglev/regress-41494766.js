// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function issue() {
  let i = 0;
  function increment() {
    i = i + 1;
  }
  while (i < 100) {
    let prevI = i;
    increment();
    assertFalse(i == prevI);
    if (i == 20) %OptimizeOsr();
  }
}
%PrepareFunctionForOptimization(issue);
issue();

function issue2() {
  let i = 0;
  function read() {
    return i;
  }
  while (i < 100) {
    var prevI = read();
    i = i + 1;
    var curI = read();
    assertFalse(prevI == curI);
    if (i == 20) %OptimizeOsr();
  }
}
%PrepareFunctionForOptimization(issue2);
issue2();

function issue3() {
  let i = 0;
  function check(x, c) {
    i = i + 1;
    c();
  }
  while (i < 100) {
    function aliasing() {
      let prevI = i;
      function check2() {
        assertTrue(prevI != i);
      }
      assertFalse(prevI != i);
      check(i, check2);
    }
    aliasing();
    if (i == 20) %OptimizeOsr();
  }
}
%PrepareFunctionForOptimization(issue3);
issue3();

inc = undefined
function issue4() {
  let i = 0;
  let first = !inc;
  if (!inc) {
    inc = function() {
      i = i + 1;
    };
    %PrepareFunctionForOptimization(inc);
  }
  while (i < 100) {
    let prevI = i;
    inc();
    if (first) {
      assertFalse(i == prevI);
    } else {
      assertTrue(i == prevI);
    }
    i = i+1;
    if (!first && i == 20) %OptimizeOsr();
  }
}
%PrepareFunctionForOptimization(issue4);
issue4();
issue4();

function issue5() {
  let i = 0;
  function increment() {
    i = i + 1;
  }
  {
    let start_block = 1;
    function capture() { return start_block; }
    while (i < 100) {
      let prevI = i;
      increment();
      assertFalse(i == prevI);
      if (i == 20) %OptimizeOsr();
    }
    return start_block;
  }
}
%PrepareFunctionForOptimization(issue5);
issue5();


(function() {
  let i = 0;
  function inc() { i = i + 1; }
  %PrepareFunctionForOptimization(inc);
  while (i < 2) {
    function aliasing() {
      let prevI = i;
      inc();
      assertFalse(prevI == i);
    }
    %PrepareFunctionForOptimization(aliasing);
    aliasing();
    %OptimizeMaglevOnNextCall(aliasing);
  }
})();


(function() {
  let i = 0;
  function inc() { i = i + 1; }
  %PrepareFunctionForOptimization(inc);
  while (i < 2) {
    let asdf = 0;
    function xxx() { return asdf; }
    function aliasing() {
      let prevI = i;
      inc();
      assertFalse(prevI == i);
    }
    %PrepareFunctionForOptimization(aliasing);
    aliasing();
    %OptimizeMaglevOnNextCall(aliasing);
  }
})();

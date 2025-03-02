// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-lazy-feedback-allocation
// Flags: --turbofan --turboshaft-string-concat-escape-analysis

class SomeError {}

function prettyPrinted(value) {
  function prettyPrint() {
    switch (typeof value) {}
  }
  return typeof value;
}

function fail(name_opt) {
  throw new SomeError(() =>   name_opt);
}

function bar(expected) {
  fail(expected + " +- " + prettyPrinted());
}

function foo() {
  for (let i = 0; i < 10; i++) {
    let x = i / 1257;

    try { bar(0); } catch (e) {}
  }
}

%PrepareFunctionForOptimization(bar);
%PrepareFunctionForOptimization(foo);
foo();

%OptimizeFunctionOnNextCall(foo);
foo();

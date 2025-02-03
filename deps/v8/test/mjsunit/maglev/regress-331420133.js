// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

var caught = 0;

function foo() {
  try {
      let a = 40;
      try {
        eval("var x = 20;");
      } catch (e) {
        caught++;
      }
  } catch (e) {}
}

%PrepareFunctionForOptimization(foo);
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

assertEquals(caught, 0);

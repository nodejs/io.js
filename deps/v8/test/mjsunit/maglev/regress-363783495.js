// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags:  --allow-natives-syntax --no-turbofan

// --no-turbofan is needed to make this is easier to repro.
// It elides some interrupt budgets checks in the code.

function bar() {
  for (let i = 0; i < 50; i++) {
    let v8 = i | 0;
    switch (v8) {
      case 28:
        if ((i != null) &&typeof i == "object") {
          function f18() {
          return v8;
        }
      }
      v8 += 1;
      return 33;
    }
  }
}

function foo() {
  bar();
}

%PrepareFunctionForOptimization(foo);
%PrepareFunctionForOptimization(bar);
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

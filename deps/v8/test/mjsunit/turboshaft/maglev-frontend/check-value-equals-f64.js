// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

var e;
function foo() {
  for (let v0 = 0; v0 < 5; v0++) {
    %OptimizeOsr();
    e = v0 ** v0;
  }
}

%PrepareFunctionForOptimization(foo);
foo();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --script-context-mutable-heap-number

let glob = 0;

function* foo() {
  glob++;
}

%PrepareFunctionForOptimization(foo);
let gen = foo();
gen.next();

%OptimizeFunctionOnNextCall(foo);
gen = foo();
gen.next();

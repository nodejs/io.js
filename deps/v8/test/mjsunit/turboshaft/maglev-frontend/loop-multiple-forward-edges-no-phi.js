// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan
// Flags: --no-maglev-loop-peeling

let counter = 1;
function g() {
  return counter-- != 0;
}
%NeverOptimizeFunction(g);

function f(c) {
  let i = 0;
  if (c) { g() }
  // The following loop will have 2 forward edges and no Phis.
  while (true) {
    if (g()) break;
  }
}

%PrepareFunctionForOptimization(f);
f(true);
f(false);

counter = 5;
%OptimizeFunctionOnNextCall(f);
f();

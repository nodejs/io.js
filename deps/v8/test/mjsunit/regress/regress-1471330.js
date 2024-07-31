// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function main() {
  for (let b = 0; b < 20; b++) {
    Math.abs(Math.max(b, -16)) - b;
  }
}

%PrepareFunctionForOptimization(main);
main();
%OptimizeFunctionOnNextCall(main);
main();

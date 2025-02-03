// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev

function foo(arg) {
  let a = 4;
  let c = true;
  try {

    while (true) {
      %PrepareFunctionForOptimization(foo);

      if (a++ == arg) %OptimizeOsr();

      while (c) {

        let d = 10;
        while (d-- > 8) {
          a++ == arg %OptimizeOsr();
        }

      }
    }

  } catch (e) {}
}

for (var i = 0; i < 13; i++) {
 foo(i);
}

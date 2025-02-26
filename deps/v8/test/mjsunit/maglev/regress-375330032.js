// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function foo(arg) {
  try {
    let v = 0n;
    for (let i = 0; i < arg, true; ++i) {
      3n + v; // This will throw on the 2nd iteration because {v} won't be a
              // BigInt anymore.
      v = arg;
      arg = i;
    }
  } catch(e) {
    return 42;
  }
  return 17;
}

%PrepareFunctionForOptimization(foo);
assertEquals(42, foo(3));

%OptimizeMaglevOnNextCall(foo);
assertEquals(42, foo(3));

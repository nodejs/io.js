// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev
// Flags: --no-concurrent-recompilation --jit-fuzzing

async function foo(x) {
  if (x) await 6;

  for (let i = 0; i < 5; i++) {
    await 42;
  }
}

for (let i = 0; i < 50; i++) {
  foo();
}

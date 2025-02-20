// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev
// Flags: --turboshaft-assert-types --jit-fuzzing

for (let i = 0; i < 50; i++) {
  function foo() { }
}

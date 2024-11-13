// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --jit-fuzzing --turboshaft-from-maglev  --no-maglev

function Foo() { }
const v17 = new Foo();

for (let v8 = 0; v8 < 32; v8++) {
  for (let i = 0; i < 1; i++) {
    function F18() {}
    if (1 === 2n) {
      v17.__proto__ = {};
    } else {
      break;
    }
  }
}

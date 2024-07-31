// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --jit-fuzzing
class C extends class {}
{
  constructor() {
    for (let i = 0; i < 5; i++) {
      const x = new Uint32Array(246);
      let [...y] = x;
      return {};
      function f() {}
    }
  }
}
new C();

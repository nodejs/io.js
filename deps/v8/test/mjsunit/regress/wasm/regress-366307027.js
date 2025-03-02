// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-max-module-size=12

function module() {
  'use asm';
  function f(x) {
    x = x | 0;
    return 2.3;
  }
  return {f: f};
}
module();

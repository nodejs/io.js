// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy --validate-asm --assert-types --trace-turbo --turboshaft-wasm-wrappers

function __f_8(stdlib, foreign, buffer) {
  "use asm";
  function __f_9(a) {
    a = a | 0;
  }
  return {__f_9: __f_9};
}

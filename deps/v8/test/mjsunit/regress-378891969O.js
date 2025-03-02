// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let x;
function foo() {
  for (var i = 0x7fff0000; i < 0x80000000; i++) {
    x = 42 + i - i;
  }
}
foo();
assertEquals(42, x)

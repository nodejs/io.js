// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug

function foo() {
  // This will assert that the value initialization was tracked correctly.
  myLet++;
}

for (var i = 0; i < 10; ++i) {
  if (i == 5) {
    %BaselineOsr();
  }
}

// This is handled in baseline code.
let myLet = 8;

foo();

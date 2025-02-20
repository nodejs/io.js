// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Create a Int32 heap slot in script context.
let a = 0;
a = 0x4fff0000;

function foo(o) {
  // Forces the load to be through the runtime C++ code.
  with (o) {
    return a != null;
  }
}
foo({});

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --always-turbofan --minimum-invocations-before-optimization=0

let x = 0;

function foo(a) {
  eval();
  x = a;
}

assertEquals(undefined, foo());

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

class C extends String {
  constructor() {
    super("bar");
    assertEquals(0, super.length);
  }
}

%PrepareFunctionForOptimization(C);
new C();
%OptimizeMaglevOnNextCall(C);
new C();

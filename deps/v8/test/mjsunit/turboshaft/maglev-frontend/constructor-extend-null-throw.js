// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

// Testing ThrowIfNotSuperConstructor (which triggers because the base class
// of A1 is "null", which doesn't have a constructor).
class A1 extends null {
  constructor() {
    super();
  }
}

%PrepareFunctionForOptimization(A1);
assertThrows(() => new A1(), TypeError,
            "Super constructor null of A1 is not a constructor");
%OptimizeFunctionOnNextCall(A1);
assertThrows(() => new A1(), TypeError,
            "Super constructor null of A1 is not a constructor");
assertOptimized(A1);

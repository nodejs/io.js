// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

// Testing ThrowSuperNotCalledIfHole (which triggers because we call
// don't call super()).
class A3 extends Object {
  constructor() {}
}

%PrepareFunctionForOptimization(A3);
assertThrows(() => new A3(), ReferenceError,
            "Must call super constructor in derived class before " +
            "accessing 'this' or returning from derived constructor");
%OptimizeFunctionOnNextCall(A3);
assertThrows(() => new A3(), ReferenceError,
            "Must call super constructor in derived class before " +
            "accessing 'this' or returning from derived constructor");
assertOptimized(A3);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

// Testing ThrowSuperAlreadyCalledIfNotHole (which triggers because we call
// super() twice).
class A2 extends Object {
  constructor() {
    super();
    super();
  }
}

%PrepareFunctionForOptimization(A2);
assertThrows(() => new A2(), ReferenceError,
            "Super constructor may only be called once");
%OptimizeFunctionOnNextCall(A2);
assertThrows(() => new A2(), ReferenceError,
            "Super constructor may only be called once");
assertOptimized(A2);

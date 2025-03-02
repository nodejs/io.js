// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

class A extends Object {
  constructor(b) {
    super();
    this.x = b;
    this.y = 42;
  }
}

class B extends A {
  constructor(b) {
    super(b);
    this.z = 12;
  }
}

%PrepareFunctionForOptimization(B);
let o = new B();
%OptimizeFunctionOnNextCall(B);
assertEquals(o, new B());
assertOptimized(B);

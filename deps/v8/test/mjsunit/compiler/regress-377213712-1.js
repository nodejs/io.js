// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

class C {
  foo() {
    return super.x;
  }
}


let o = new C();
%PrepareFunctionForOptimization(o.foo);
assertEquals(undefined, o.foo());

o.__proto__.__proto__ = new String("bla");
assertEquals(undefined, o.foo());

%OptimizeFunctionOnNextCall(o.foo);
assertEquals(undefined, o.foo());

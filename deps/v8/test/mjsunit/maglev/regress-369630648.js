// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

class C extends Array {
  constructor() {
      (() => (() => super())())();
  }
}
%PrepareFunctionForOptimization(C);
new C();
new C();
%OptimizeFunctionOnNextCall(C);
new C();
C.__proto__ = [1];
assertThrows(() => { new C() }, TypeError);

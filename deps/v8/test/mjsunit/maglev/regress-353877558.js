// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

class C0 {
  constructor() {
      for (let v3 = 0; v3 < 25; v3++) {
      }
      const t4 = [3.523949138304181e+307];
      t4[4] = 256;
  }
}
%PrepareFunctionForOptimization(C0);
new C0;
%OptimizeFunctionOnNextCall(C0);
new C0();

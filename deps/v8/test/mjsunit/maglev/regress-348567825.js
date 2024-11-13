// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax

function f1() {
    let v2 = 1;
    let v3 = 0;
    for (let i4 = v2; v2 >>> i4, f1; i4 += v2) {
      if (v3 == 10) {
        %OptimizeOsr();
      }
      if (v3 == 40) {
          break;
      }
      v2 = v2 && i4;
      v3++;
    }
}
%PrepareFunctionForOptimization(f1);
f1();

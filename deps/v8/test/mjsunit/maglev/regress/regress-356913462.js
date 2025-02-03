// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev


function __f_1() {
  var x = 2;
  while (x-- > 0) {
    x = x;
    let o = {
      get: function () {
        x[638197]();
      }
    };
    %OptimizeOsr();
  }
  return x;
}
%PrepareFunctionForOptimization(__f_1);
assertTrue(__f_1() === -1);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function fun() {
  var f = [];
  for (let v2 = 0; v2 < 5; v2++) {
      const v3 = %OptimizeOsr();
      const v5 = !(v2 ?? -0.0);
      var f = v5;
      -0.0 != v5;
  }
}
%PrepareFunctionForOptimization(fun);
fun();

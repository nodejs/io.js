// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-future

function f0() {
  const v5 = [];
  v5.d = undefined;
  for (let v6 = 0; v6 < 5; v6++) {
    function f7(a8, ) {
      a8.d = v6;
    }
    %PrepareFunctionForOptimization(f7);
    [-1225281237,v5].reduceRight(f7);
  }
}

%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();

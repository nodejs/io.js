// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function f0() {
  try {
      for (let i5 = 0, i6 = 10;
          (() => {
              const v7 = [-4.0,-8.898103618409834];
              v7.keys().next( i5);
              i6--;
              return i5 < i6;
          })();
          ) {
      }
      v1[Symbol]();
  } catch(e17) {
  }
}
%PrepareFunctionForOptimization(f0);
f0();
%OptimizeFunctionOnNextCall(f0);
f0();

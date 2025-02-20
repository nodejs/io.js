// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function test() {
  for (let v0 = 0; v0 < 5; v0++) {
      let v1 = 35;
      while ((() => {
              const v2 = v1--;
              function f3() {
              }
              function f4() {
                  let v5 = 0;
                  for (let v6 = 0; v6 < 5; v6++) {
                      switch (v6) {
                          case 4:
                              v5 = f3;
                      }
                  }
              }
              f4();
              return v2 > 31;
          })()) {
          const v11 = %OptimizeOsr();
      }
  }
}
%PrepareFunctionForOptimization(test);
test();

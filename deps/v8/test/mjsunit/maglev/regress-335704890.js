// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax
// Flags: --no-lazy-feedback-allocation --invocation-count-for-maglev=1
// Flags: --invocation-count-for-osr=1 --invocation-count-for-maglev-osr=1

function f0() {
  function f5() {
      function f7() {
          const o12 = {
          };
          o12.g = o12;
          const v13 = o12.g;
          return v13 + v13;
      }
      f7();
      return arguments;
  }
  f5();
}

for (let j = 0; j < 50; j++) {
  f0();
}

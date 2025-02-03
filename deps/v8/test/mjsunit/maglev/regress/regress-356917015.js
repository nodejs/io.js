// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function f0() {
  function f1(a2) {
      if (a2) {
          throw_before_this_function_is_not_defined();
      }
  }
  const v5 = %NeverOptimizeFunction(f1);
  let v7 = undefined >>> undefined;
  try {
      f1();
      v7 = 45;
      f1(true);
  } catch(e12) {
      v7 + 3;
  }
  for (let v15 = 0; v15 < 100; v15++) {
  }
}
const v16 = %PrepareFunctionForOptimization(f0);
f0();
f0();

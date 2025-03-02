// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbolev --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan

function test() {
  for (let i = 0; i < 6200; i++) {
    function f(__v_9, __v_10) {
      this.name = i;
      __v_9.name = __v_10;
      for (let __v_13 = 0; __v_13 < 100; __v_13++) {}
    }
    %PrepareFunctionForOptimization(f);
    const __v_4 = new f(i);
    new f(__v_4);
  }
}

%PrepareFunctionForOptimization(test);

test();

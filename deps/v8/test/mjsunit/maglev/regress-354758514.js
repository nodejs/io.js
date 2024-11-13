// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

__v_0 = new Proxy({}, {getOwnPropertyDescriptor:  {  }});

function __f_0() {
  throw 5;
}
function __f_1() {
  try {
    with ({ __v_6 : __v_0 + 42 }) {
      __f_0();
    }
  } catch(e) {
  }
}

%PrepareFunctionForOptimization(__f_1);
__f_1();
__f_1();
%OptimizeFunctionOnNextCall(__f_1);
__f_1();

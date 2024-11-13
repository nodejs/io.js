// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --always-turbofan

let nop = function () { };
function func(args) {
  var bar = 0;
  for (var arg in args) {
    try {
      for (var i = 0; i < 2; i++) {
        nop();
        bar += i[arg];
      }
    } catch (e) { }
  }
}
%PrepareFunctionForOptimization(func);
%OptimizeFunctionOnNextCall(func);
func({x: 20, y: 11});

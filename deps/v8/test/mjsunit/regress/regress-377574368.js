// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft --turbolev

function f(a) {
  return 'bad value: ' + a;
}
%PrepareFunctionForOptimization(f);

function g() {
  try {
    f(new String('foo'));
    throw 'lol';
  } catch (e) { }
}
%PrepareFunctionForOptimization(g);

g();
g();

%OptimizeFunctionOnNextCall(g);

g();

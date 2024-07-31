// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --max-valid-polymorphic-map-count=20

function f1(o) { return o.hasOwnProperty(0); }
function f2(o) { return o.hasOwnProperty(0); }
function f3(o) { return o.hasOwnProperty(0); }
function f4(o) { return o.hasOwnProperty(0); }
function f5(o) { return o.hasOwnProperty(0); }
function f6(o) { return o.hasOwnProperty(0); }
function f7(o) { return o.hasOwnProperty(0); }
function f8(o) { return o.hasOwnProperty(0); }
function f9(o) { return o.hasOwnProperty(0); }
function f10(o) { return o.hasOwnProperty(0); }

const funcs = [f1, f2, f3, f4, f5, f6, f7, f8, f9, f10];
for (f of funcs) {
  %PrepareFunctionForOptimization(f);
}

const objects = [];
for (let i = 0; i < 10; ++i) {
  // This creates a new map because the prototype is a different
  // object each time.
  objects.push({__proto__: {}});
}

// Make f1 see 1 maps, f2 2 maps etc.
for (let f_ix = 0; f_ix < funcs.length; ++f_ix) {
  for (let o_ix = 0; o_ix <= f_ix; ++o_ix) {
    funcs[f_ix](objects[o_ix]);
  }
}

for (f of funcs) {
  %OptimizeMaglevOnNextCall(f);
}

for (f of funcs) {
  f(objects[0]);
}

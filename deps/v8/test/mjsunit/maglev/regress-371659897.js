// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev-licm --no-maglev-loop-peeling

function foo(x, y) {
  let once = false;
  while (x.foo == y) {
    if (once) return x;
    once = true;
  }
  return x
}

%PrepareFunctionForOptimization(foo);
foo({foo: 1}, 1);
// Deopt to disable map hoisting
%OptimizeMaglevOnNextCall(foo);
foo({foo: 1}, 1);
foo({foo: 1}, 1.1);
// Deopt with actually failing map check
%OptimizeMaglevOnNextCall(foo);
foo({foo: 1}, 1);
foo({x: {}}, 1);

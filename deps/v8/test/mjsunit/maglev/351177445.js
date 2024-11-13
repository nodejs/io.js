// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

async function f1(x) {
  for (let i = 0; i < 1; i++) {
    try { i[i](23n, 12); } catch {}
  }
  try {
    await "-4294967296";
  } catch {}
  try {
    Object.defineProperty(this,  'x', {
      set: function (value) {x;}
    });
  } catch {}
}
%PrepareFunctionForOptimization(f1);
try {f1()} catch {};
%OptimizeFunctionOnNextCall(f1);
try {f1()} catch {};

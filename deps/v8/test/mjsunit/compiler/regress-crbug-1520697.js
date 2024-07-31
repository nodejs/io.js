// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function main() {
  function f0(v3, v4, v5) {
    return (v3 | 0) + (v4 | 0) + (v5 | 0);
  }
  function f1(v6) {
    var v7 = v6.f;
    var v8 = v6.g;
    var v9 = v6.h;
    try {
      var v10 = v6.i;
    } catch (e) {}
    try {
      var v11 = v6.j;
    } catch (e) {}
    var v12 = v6.k;
    var v13 = v6.l;
    return f0(42, void 0, void 0) + v7 + v8 + v9 + v10 + v11 + v12 + v13;
  }
  for (var v0 = 0; v0 < 100000; ++v0) {
    var v1 = f1({
      f: v0 * 3,
      g: v0 - 1,
      h: v0 / 2 | 0,
      i: -v0,
      j: 13 + (v0 / 5 | 0),
      k: 14 - (v0 / 6 | 0),
      l: 1 - v0
    });
    var v2 = 42 + v0 * 3 + v0 - 1 + (v0 / 2 | 0) - v0 + 13 + (v0 / 5 | 0) + 14 - (v0 / 6 | 0) + 1 - v0;
    if (v1 != v2) v0 + " " + v2 + " " + v1;
  }
}

main();
%PrepareFunctionForOptimization(main);
main();
%OptimizeFunctionOnNextCall(main);
main();

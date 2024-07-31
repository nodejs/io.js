// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --const-tracking-let --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --maglev --no-stress-maglev
// Flags: --sparkplug --no-always-sparkplug --stress-ic

let a0 = 0;
let a1 = 0;
let a2 = 0;
let a3 = 0;
let a4 = 0;
let a5 = 0;
let a6 = 0;
let a7 = 0;
let a8 = 0;
let a9 = 0;
let a10 = 0;
let a11 = 0;
let a12 = 0;
let a13 = 0;
let a14 = 0;
let a15 = 0;
let a16 = 0;
let a17 = 0;
let a18 = 0;
let a19 = 0;
let a20 = 0;
let a21 = 0;
let a22 = 0;
let a23 = 0;
let a24 = 0;
let a25 = 0;
let a26 = 0;
let a27 = 0;
let a28 = 0;
let a29 = 0;
let a30 = 0;
let a31 = 0;
let a32 = 0;
let a33 = 0;
let a34 = 0;
let a35 = 0;
let a36 = 0;
let a37 = 0;
let a38 = 0;
let a39 = 0;
let a40 = 0;
let a41 = 0;
let a42 = 0;
let a43 = 0;
let a44 = 0;
let a45 = 0;
let a46 = 0;
let a47 = 0;
let a48 = 0;
let a49 = 0;
let a50 = 0;
let a51 = 0;
let a52 = 0;
let a53 = 0;
let a54 = 0;
let a55 = 0;
let a56 = 0;
let a57 = 0;
let a58 = 0;
let a59 = 0;
let a60 = 0;
let a61 = 0;
let a62 = 0;
let a63 = 0;
let a64 = 0;
let a65 = 0;
let a66 = 0;
let a67 = 0;
let a68 = 0;
let a69 = 0;
let a70 = 0;
let a71 = 0;
let a72 = 0;
let a73 = 0;
let a74 = 0;
let a75 = 0;
let a76 = 0;
let a77 = 0;
let a78 = 0;
let a79 = 0;
let a80 = 0;
let a81 = 0;
let a82 = 0;
let a83 = 0;
let a84 = 0;
let a85 = 0;
let a86 = 0;
let a87 = 0;
let a88 = 0;
let a89 = 0;
let a90 = 0;
let a91 = 0;
let a92 = 0;
let a93 = 0;
let a94 = 0;
let a95 = 0;
let a96 = 0;
let a97 = 0;
let a98 = 0;
let a99 = 0;

for (let i = 0; i < 100; ++i) {
  // Use RuntimeEvaluateREPL so that set will use the StaGlobal bytecode.
  %RuntimeEvaluateREPL('set = function set(newValue) {a' + i + ' = newValue;}');
  %PrepareFunctionForOptimization(set);
  set(0);
  eval('read = function read() { return a' + i + ';}');
  %PrepareFunctionForOptimization(read);
  read();
  %OptimizeFunctionOnNextCall(read);
  assertEquals(0, read());
  set(123);
  assertEquals(123, read());
}

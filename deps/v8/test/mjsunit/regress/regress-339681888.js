// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-externalize-string --allow-natives-syntax

function Constant() {
  return String.fromCharCode(0x100_000);
}

const kCode = 0x100_000;
function NonConstant(x) {
  return String.fromCharCode(x);
}

function Shenanigans(string_maker) {
  let array = new Array(1000);
  array[1000] = string_maker();
  let long = array.join("abc");
  let slice = long.substring(0, 1000);
  // Internalize {long}. The first time this is executed, the string will
  // be internalized in-place; from the second time onwards it'll be turned
  // into a ThinString.
  let dummy = {};
  dummy[long] = "internalized!";
  let test = slice.replace(/abc/uim, ()=>{});
  assertEquals("undefined", test.substring(0, 9));
}

%PrepareFunctionForOptimization(Constant);
%PrepareFunctionForOptimization(NonConstant);

for (let i = 0; i < 3; i++) {
  assertTrue(isOneByteString(Constant()));
  assertTrue(isOneByteString(NonConstant(kCode)));
  Shenanigans(Constant);
  Shenanigans(NonConstant);
}
%OptimizeFunctionOnNextCall(Constant);
%OptimizeFunctionOnNextCall(NonConstant);

// Part I: If we get a two-byte string, and use it for further shenanigans,
// that shouldn't trip up any DCHECKs.
Shenanigans(Constant);
Shenanigans(NonConstant);

// Part II: We shouldn't be getting two-byte strings.
assertTrue(isOneByteString(Constant()));
assertTrue(isOneByteString(NonConstant(kCode)));

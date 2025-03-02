// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function CreateResizableArrayBuffer(byteLength, maxByteLength) {
  return new ArrayBuffer(byteLength, {
    maxByteLength: maxByteLength
  });
}

function foo(val) {
  return val.length;
}

let arr = new ArrayBuffer(32);
let intArr = new Int32Array(arr);

%PrepareFunctionForOptimization(foo);
foo(intArr);
foo(new Int32Array(CreateResizableArrayBuffer(16, 40)));
arr.transfer(764230);
%OptimizeFunctionOnNextCall(foo);
assertEquals(0, foo(intArr));

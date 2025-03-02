// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function getByteOffset(arr) {
  return arr.byteOffset;
}

%PrepareFunctionForOptimization(getByteOffset);

{
  let buffer = new ArrayBuffer(128, {maxByteLength: 1024});
  let arr = new Uint8Array(buffer, 64, 4);

  assertEquals(64, getByteOffset(arr));
  buffer.resize(0);
  assertEquals(0, getByteOffset(arr));
}

%OptimizeFunctionOnNextCall(getByteOffset);

{
  let buffer = new ArrayBuffer(128, {maxByteLength: 1024});
  let arr = new Uint8Array(buffer, 64, 4);

  assertEquals(64, getByteOffset(arr));
  buffer.resize(0);
  assertEquals(0, getByteOffset(arr));
}

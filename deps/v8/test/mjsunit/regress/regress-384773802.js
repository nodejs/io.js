// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function getLength() { return arr.length; }

var buf = new ArrayBuffer(0x10000);
var arr = new Uint8Array(buf);

%ArrayBufferDetach(arr.buffer);
%PrepareFunctionForOptimization(getLength);
assertEquals(0, getLength());
%OptimizeFunctionOnNextCall(getLength);
assertEquals(0, getLength());

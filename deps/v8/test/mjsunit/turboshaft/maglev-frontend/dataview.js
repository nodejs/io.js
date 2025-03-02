// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

function dataview() {
  const buffer = new ArrayBuffer(40);
  const dw = new DataView(buffer);
  dw.setInt8(4, 32);
  dw.setInt16(2, 152515);
  dw.setFloat64(8, 12.2536);
  return [dw.getInt16(0), dw.getInt8(4), dw.getFloat64(2), dw.getInt32(7)];
}

%PrepareFunctionForOptimization(dataview);
dataview();
let a1 = dataview();
%OptimizeFunctionOnNextCall(dataview);
let a2 = dataview();
assertEquals(a1, a2);
assertOptimized(dataview);

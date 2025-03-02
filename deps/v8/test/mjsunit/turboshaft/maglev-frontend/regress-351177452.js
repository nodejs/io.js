// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan

var buffer = new ArrayBuffer(64);
var dataview = new DataView(buffer);
var little_endian = [-3]; // This evaluates to true

function get_int16(offset) {
  return dataview.getInt16(offset, little_endian);
}

dataview.setInt16(10, 0x1234);

%PrepareFunctionForOptimization(get_int16);
get_int16();

%OptimizeFunctionOnNextCall(get_int16);
assertEquals(0x3412, get_int16(10));

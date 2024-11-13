// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

var buffer = new ArrayBuffer(64);
var dataview = new DataView(buffer, 8, 24);

var values = [-1, 2, -3, 42];

function readUint8(offset) {
  return dataview.getUint8(offset);
}

function readInt8Handled(offset) {
  try {
    return dataview.getInt8(offset);
  } catch (e) {
    return e;
  }
}

function readUint16(offset, little_endian) {
  return dataview.getUint16(offset, little_endian);
}

function readInt16Handled(offset, little_endian) {
  try {
    return dataview.getInt16(offset, little_endian);
  } catch (e) {
    return e;
  }
}

function readUint32(offset, little_endian) {
  return dataview.getUint32(offset, little_endian);
}

function readInt32Handled(offset, little_endian) {
  try {
    return dataview.getInt32(offset, little_endian);
  } catch (e) {
    return e;
  }
}

function readFloat32(offset, little_endian) {
  return dataview.getFloat32(offset, little_endian);
}

function readFloat64(offset, little_endian) {
  return dataview.getFloat64(offset, little_endian);
}

function readBigInt64Handled(offset, little_endian) {
  try {
    return dataview.getBigInt64(offset, little_endian);
  } catch (e) {
    return e;
  }
}

function readBigUint64Handled(offset, little_endian) {
  try {
    return dataview.getBigUint64(offset, little_endian);
  } catch(e) {
    return e;
  }
}

function warmup(f) {
  %PrepareFunctionForOptimization(f);
  f(0);
  f(1);
  %OptimizeFunctionOnNextCall(f);
  f(2);
  f(3);
}

// TurboFan valid getInt8.
for (var i = 0; i < values.length; i++) {
  dataview.setInt8(i, values[i]);
}
warmup(readInt8Handled);
assertOptimized(readInt8Handled);
assertEquals(values[0], readInt8Handled(0));
assertEquals(values[1], readInt8Handled(1));
assertEquals(values[2], readInt8Handled(2));
assertEquals(values[3], readInt8Handled(3));

// TurboFan valid getUint8.
dataview.setUint32(4, 0xdeadbeef);
warmup(readUint8);
assertOptimized(readUint8);
assertEquals(0xde, readUint8(4));
assertEquals(0xad, readUint8(5));
assertEquals(0xbe, readUint8(6));
assertEquals(0xef, readUint8(7));

// TurboFan valid getUint16.
dataview.setUint16(8, 0xabcd);
warmup(readUint16);
assertOptimized(readUint16);
assertEquals(0xabcd, readUint16(8));
assertEquals(0xcdab, readUint16(8, true));

// TurboFan valid getInt16.
let b1 = -0x1234;
dataview.setInt16(10, b1);
warmup(readInt16Handled);
assertOptimized(readInt16Handled);
assertEquals(b1, readInt16Handled(10));
dataview.setInt16(10, b1, true);
assertEquals(b1, readInt16Handled(10, true));

// TurboFan valid getUint32.
dataview.setUint32(12, 0xabcdef12);
warmup(readUint32);
assertOptimized(readUint32);
assertEquals(0xabcdef12, readUint32(12));
assertEquals(0x12efcdab, readUint32(12, true));

// TurboFan valid getInt32.
let b2 = -0x12345678;
dataview.setInt32(16, b2);
warmup(readInt32Handled);
assertOptimized(readInt32Handled);
assertEquals(b2, readInt32Handled(16));
dataview.setInt32(16, b2, true);
assertEquals(b2, readInt32Handled(16, true));

// TurboFan valid getFloat32.
let b3 = Math.fround(Math.E); // Round Math.E to float32.
dataview.setFloat32(16, b3);
warmup(readFloat32);
assertOptimized(readFloat32);
assertEquals(b3, readFloat32(16));
dataview.setFloat32(16, b3, true);
assertEquals(b3, readFloat32(16, true));

// TurboFan valid getFloat64.
let b4 = Math.PI;
dataview.setFloat64(16, b4);
warmup(readFloat64);
assertOptimized(readFloat64);
assertEquals(b4, readFloat64(16));
dataview.setFloat64(16, b4, true);
assertEquals(b4, readFloat64(16, true));

// TurboFan valid getBigInt64.
let b5 = -0x12345678912345n;
dataview.setBigInt64(16, b5);
warmup(readBigInt64Handled);
assertOptimized(readBigInt64Handled);
assertEquals(b5, readBigInt64Handled(16));
dataview.setBigInt64(16, b5, true);
assertEquals(b5, readBigInt64Handled(16, true));

// TurboFan valid getBigUint64.
let b6 = 0x12345678912345n;
dataview.setBigUint64(16, b6);
warmup(readBigUint64Handled);
assertOptimized(readBigUint64Handled);
assertEquals(b6, readBigUint64Handled(16));
dataview.setBigUint64(16, b6, true);
assertEquals(b6, readBigUint64Handled(16, true));

// TurboFan out of bounds reads deopt.
assertOptimized(readInt8Handled);
assertInstanceof(readInt8Handled(24), RangeError);
assertUnoptimized(readInt8Handled);
assertOptimized(readInt16Handled);
assertInstanceof(readInt16Handled(23), RangeError);
assertUnoptimized(readInt16Handled);
assertOptimized(readInt32Handled);
assertInstanceof(readInt32Handled(21), RangeError);
assertUnoptimized(readInt32Handled);

// Without exception handler.
assertOptimized(readUint8);
assertThrows(() => readUint8(24));
assertUnoptimized(readUint8);
assertOptimized(readFloat32);
assertThrows(() => readFloat32(21));
assertUnoptimized(readFloat32);
assertOptimized(readFloat64);
assertThrows(() => readFloat64(17));
assertUnoptimized(readFloat64);

// Negative Smi deopts.
(function() {
  function readInt8Handled(offset) {
    try { return dataview.getInt8(offset); } catch (e) { return e; }
  }
  warmup(readInt8Handled);
  assertOptimized(readInt8Handled);
  assertInstanceof(readInt8Handled(-1), RangeError);
  assertUnoptimized(readInt8Handled);
})();

// Non-Smi index deopts.
(function() {
  function readUint8(offset) { return dataview.getUint8(offset); }
  warmup(readUint8);
  assertOptimized(readUint8);
  assertEquals(values[3], readUint8(3.14));
  assertUnoptimized(readUint8);
})();

// TurboFan detached buffer deopts.
(function() {
  function readInt8Handled(offset) {
    try { return dataview.getInt8(offset); } catch (e) { return e; }
  }
  warmup(readInt8Handled);
  assertOptimized(readInt8Handled);
  %ArrayBufferDetach(buffer);
  assertInstanceof(readInt8Handled(0), TypeError);
  assertUnoptimized(readInt8Handled);
})();

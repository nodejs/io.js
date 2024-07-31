// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab = new ArrayBuffer(3782, {maxByteLength: 4096});
const u16a = new Int16Array(rab);
rab.resize(0);

function ctor() {
  return u16a;
}

assertThrows(() => { Float64Array.of.call(ctor, 1); }, TypeError);

rab.resize(8);
const u16a2 = Int16Array.of.call(ctor, 3);
assertEquals(3, u16a2[0]);

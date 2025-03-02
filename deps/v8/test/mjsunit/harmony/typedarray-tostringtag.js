// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

d8.file.execute('test/mjsunit/typedarray-helpers.js');

const sab = new SharedArrayBuffer(4 * 8);
const rab = CreateResizableArrayBuffer(4 * 8, 8 * 8);
const gsab = CreateGrowableSharedArrayBuffer(4 * 8, 8 * 8);

for (let TA of builtinCtors) {
  const backedByAB = new TA();
  const backedBySAB = new TA(sab);
  const backedByRAB = new TA(rab);
  const backedByGSAB = new TA(gsab);
  const expected = `[object ${TA.name}]`;
  assertEquals(expected, Object.prototype.toString.call(backedByAB));
  assertEquals(expected, Object.prototype.toString.call(backedBySAB));
  assertEquals(expected, Object.prototype.toString.call(backedByRAB));
  assertEquals(expected, Object.prototype.toString.call(backedByGSAB));
}

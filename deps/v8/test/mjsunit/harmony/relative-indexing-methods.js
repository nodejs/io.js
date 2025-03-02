// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

'use strict';

[
  [1, 2, 3],
  '123',
  new Uint8Array([1, 2, 3]),
].forEach((v) => {
  assertEquals(v.at(0), v[0]);
  assertEquals(v.at(-1), v[2]);
  assertEquals(v.at(-4), undefined);
  assertEquals(v.at(3), undefined);
  assertEquals(v.at(1337), undefined);
  assertEquals(v.at(-Infinity), undefined);
  assertEquals(v.at(Infinity), undefined);
  assertEquals(v.at(NaN), v[0]);
  assertEquals(v.at(undefined), v[0])
  assertEquals(v.at(), v[0]);
});

{
  const props = ['length', '2'];
  const proxy = new Proxy([1, 2, 3], {
    get(t, p, r) {
      assertEquals(p, props.shift());
      return Reflect.get(t, p, r);
    }
  });
  assertEquals(Array.prototype.at.call(proxy, -1), 3);
}

assertThrows(() => {
  Uint8Array.prototype.at.call({});
}, TypeError);

{
  const a = new Uint8Array([1, 2, 3]);
  %ArrayBufferDetach(a.buffer);
  assertThrows(() => {
    a.at(0);
  }, TypeError);
}

assertEquals(Array.prototype[Symbol.unscopables].at, true);

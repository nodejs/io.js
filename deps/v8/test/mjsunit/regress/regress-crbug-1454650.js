// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const gsab = new SharedArrayBuffer(100, {maxByteLength: 200});
const ta = new Int8Array(1);
class c extends Int8Array {
  constructor() {
    super(gsab);
  }
}
ta.constructor = c;
const mapper_params = [];
function mapper(x) {
  mapper_params.push(x);
  return x + 1;
}
const mapped = ta.map(mapper);
assertEquals([0], mapper_params);
// `mapped` is length-tracking with `gsab` as a backing buffer.
assertEquals(gsab.byteLength, mapped.length);
assertEquals(1, mapped[0]);
assertEquals(gsab, mapped.buffer);

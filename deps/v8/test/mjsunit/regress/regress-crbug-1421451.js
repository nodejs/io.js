// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const ab = new ArrayBuffer(1000, {"maxByteLength": 1000});
const ta = new Int16Array(ab);

let mapperCallCount = 0;
function evilMapper() {
  ++mapperCallCount;
  ab.resize(0);
}

function evilCtor() {
  return ta;
}

Float64Array.from.call(evilCtor, [0, 1], evilMapper);
assertEquals(2, mapperCallCount);

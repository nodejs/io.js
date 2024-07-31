// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab1 = new ArrayBuffer(4, {"maxByteLength": 100});
const ta = new Int8Array(rab1);
const rab2 = new ArrayBuffer(10, {"maxByteLength": 20});
const lengthTracking = new Int8Array(rab2);
rab2.resize(0);
ta.constructor = { [Symbol.species]: function() { return lengthTracking; } };
assertThrows(() => { ta.filter(() => { return true; }); },
             TypeError);

// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab = new ArrayBuffer(363, {"maxByteLength": 1000});
const ta = new Uint8Array(rab);
rab.resize(80);
const data = d8.serializer.serialize(ta);
const dataArray = new Uint8Array(data);
dataArray[dataArray.length - 1] = 17;
assertThrows(() => { d8.serializer.deserialize(data); });

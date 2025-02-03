// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab = new ArrayBuffer(50, {"maxByteLength": 100});
const ta = new Int8Array(rab);
const start = {};
start.valueOf = function() {
  rab.resize(0);
  return 5;
}
ta.fill(5, start);

// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const proxy = new Proxy(Int16Array, {"get": () => { throw 'lol'; }});
const rab = new ArrayBuffer(1632, {"maxByteLength": 4096});
try {
  new proxy(rab);
} catch {
}

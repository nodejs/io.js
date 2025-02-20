// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab = new ArrayBuffer(ArrayBuffer, {"maxByteLength": 7158170});
const ta = new Uint8Array(rab);
const proxy = new Proxy(ta, {});
proxy.valueOf = () => {};
assertThrows(() => { Object.seal(proxy); }, TypeError);

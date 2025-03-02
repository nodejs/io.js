// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --single-threaded

let v2 = new Int32Array(4096);
function F4( a7, a8) {
  if (!new.target) { throw 'must be called with new'; }
  function f9() {
      return a7;
  }
  function f10(a11) {
    for (const v12 of v2) {
        ({"g":v2,} = a11);
    }
    class C14 extends C13 {}
    return C14(- -1059503957, -1059503957, a8);
  }
  Object.defineProperty(this, "e", { configurable: true, get: f9, set: f10 });
  this.e = -1059503957;
}
try {
  new F4(-1059503957, -10595039571059503957);
}
catch {}

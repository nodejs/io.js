// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f() {
  const arr = new Uint8Array([
    0, 97, 115, 109, 1, 0, 0, 0, 1, 11, 2, 96, 2, 126, 124, 1, 126, 96, 0, 1,
    124, 3, 2, 1, 0, 4, 5, 1, 112, 1, 6, 6, 10, 114, 1, 112, 4, 244, 2, 127,
    118, 126, 31, 125, 1, 124, 2, 126, 2, 64, 2, 127, 3, 64, 2, 124, 3, 127,
    65, 0, 65, 0, 13, 3, 33, 142, 1, 65, 0, 13, 0, 65, 0, 11, 65, 0, 13, 2, 34,
    143, 1, 34, 241, 2, 34, 191, 2, 34, 242, 2, 34, 208, 2, 33, 144, 1, 2, 127,
    65, 0, 4, 64, 65, 0, 13, 5, 11, 65, 0, 11, 33, 145, 1, 65, 0, 17, 1, 0, 11,
    33, 139, 4, 65, 0, 13, 0, 11, 12, 1, 11, 13, 0, 11, 66, 128, 127, 34, 189,
    3, 65, 0, 13, 0, 34, 199, 3, 11, 11
  ]);
  WebAssembly.instantiate(arr);

  function g() {
    Realm.createAllowCrossRealmAccess();
    return this;
  }

  Object.defineProperty(this.d8.__proto__, "then", {"get": g});
  return "then";
}

const worker = new Worker(f, {"type": "function"});
worker.getMessage();

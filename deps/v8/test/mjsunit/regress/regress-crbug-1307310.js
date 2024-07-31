// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const gsab = new SharedArrayBuffer(4, {
    maxByteLength: 8
});
const ta = new Int8Array(gsab);

function defineUndefined(ta) {
  Object.defineProperty(ta, undefined, {
      get: function () {}
  });
}

defineUndefined(ta);
assertThrows(() => { defineUndefined(ta); });

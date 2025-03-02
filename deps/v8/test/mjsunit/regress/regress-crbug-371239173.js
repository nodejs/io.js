// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const ta = new BigUint64Array();
const o = {
  __proto__: ta,
};
const evil = {
  valueOf() {
    Object.setPrototypeOf(o, {});
    // After this, the TypedArray is no longer in the prototype chain.
    return -4n;
  },
};

o[1960] = evil; // OOB write

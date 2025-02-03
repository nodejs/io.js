// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

const rab = new ArrayBuffer(1744, {"maxByteLength": 4000});
let callSlice = true;
class MyFloat64Array extends Float64Array {
  constructor() {
    super(rab);
    if (callSlice) {
      callSlice = false;  // Prevent recursion
      super.slice();
    }
  }
};
new MyFloat64Array();

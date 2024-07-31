// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function* __getObject() {
    let obj = this['__v_1'];
    yield obj;
}
var __v_0 = 0;
var __v_1 = {
  get x() {
    __v_0++;
  }
};
var __v_2 = {
};
__v_2[0] = 6;
Object.defineProperty(__v_2, 0, {
  get: function () {
    __v_1["x"];
    return __getObject();
  },
});
JSON.stringify(__v_2);
assertEquals(1, __v_0);

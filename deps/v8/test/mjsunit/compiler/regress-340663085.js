// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f(b) {
  var v1 = {};
  v1.prop0 = 1;

  var v2 = {};
  v2.prop0 = 1;

  if (b) {
    v2 = v1;
  }

  (function (v3) {
    v3.prop0;
    v2.a = 11;
    v2.b = 12;
    v3.x = 21;
    v2.y = 22;
  })(v1);

  return v2.x;
}

assertEquals(undefined, f());
assertEquals(21, f(true));
assertEquals(21, f(true));

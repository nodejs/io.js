// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {
  var obj = {};
  var val = {};
  Object.defineProperty(obj, "x", {
    value: val,
    enumerable: true
  });
  val[10000] = [];
  var clone = { ...obj};
  obj = {};
  Object.defineProperty(obj, "x", {
    enumerable: true
  });
  clone = { ...obj };
})();

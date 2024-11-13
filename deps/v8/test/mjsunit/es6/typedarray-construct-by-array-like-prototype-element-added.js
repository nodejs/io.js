// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

(function() {
  var arr = [0, 1, , 3];
  Array.prototype[2] = 2;

  var constructors = [
    Uint8Array,
    Int8Array,
    Uint16Array,
    Int16Array,
    Uint32Array,
    Int32Array,
    Float32Array,
    Float64Array,
    Uint8ClampedArray
  ];

  for (var constr of constructors) {
    var ta = new constr(arr);
    assertArrayEquals([0, 1, 2, 3], ta);
  }
})();

(function testTypedArrayConstructByArrayLikeInvalidArrayProtector() {
  Array.prototype[2] = undefined;
  d8.file.execute("test/mjsunit/es6/typedarray-construct-by-array-like.js");
})();

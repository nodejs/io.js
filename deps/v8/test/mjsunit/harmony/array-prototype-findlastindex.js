// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function () {
  var array = [,];

  function findLastIndex() {
    return array.findLastIndex(v => v > 0);
  }

  assertEquals(findLastIndex(), -1);

  array.__proto__.push(6);
  assertEquals(findLastIndex(), 0);

  array = [6, -1, 5];
  assertEquals(findLastIndex(), 2);
})();

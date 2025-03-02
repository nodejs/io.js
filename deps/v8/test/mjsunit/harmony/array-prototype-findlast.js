// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function () {
  var array = [,];

  function findLast() {
    return array.findLast(v => v > 0);
  }

  assertEquals(findLast(), undefined);

  array.__proto__.push(6);
  assertEquals(findLast(), 6);

  array = [6, -1, 5];
  assertEquals(findLast(), 5);
})();

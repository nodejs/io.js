// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


var findLast = 'local findLast';
var findLastIndex = 'local findLastIndex';

var array = [];

with (array) {
  assertEquals('local findLast', findLast);
  assertEquals('local findLastIndex', findLastIndex);
}

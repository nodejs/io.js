// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const k0 = {key: 0};
const k1 = {key: 1};
const k2 = {key: 2};

(function TestProtoModification() {
  var input = new Set();
  input.add(k0);
  input.add(k1);
  input.add(k2);
  Set.prototype.foo = function() {};
  var s = new Set(input.values());
  assertEquals(3, s.size);
  assertTrue(s.has(k0));
  assertTrue(s.has(k1));
  assertTrue(s.has(k2));
})();

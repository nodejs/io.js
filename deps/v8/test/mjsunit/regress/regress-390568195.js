// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function create() {
  const obj = {};
  for(var i = 0; i < 700000; ++i) {
    obj['prop' + i] = 0;
  }
  obj['ref'] = 3;
  assertEquals(3, obj['ref']);
  return obj;
}

const obj = create();
assertEquals(3, obj['ref']);

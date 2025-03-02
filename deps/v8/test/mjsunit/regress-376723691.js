// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var __hash = 0;
function hashCode(str) {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    const char = String.prototype.charCodeAt.call(str, i);
    hash = (hash << 5) - hash + char;
    hash = hash & hash;
  }
  return hash;
}

function foo (value) {
  __hash = hashCode(value + __hash.toString());
};

for (var i = 0; i < 10; i++) {
  foo();
}

let seed = 1;
seed += 4294967296;
foo(seed);
seed += 4294967296;
foo(seed);

assertEquals(__hash.toString(), "1131512834");

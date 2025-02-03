// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ensure normalization involving a proto tansition works.

const p = {};
function f8(x) {
  return {
    "a": x,
    ...{},
    get g() {},
    __proto__: p,
  };
}
f8(-1024);
f8(1094813926);

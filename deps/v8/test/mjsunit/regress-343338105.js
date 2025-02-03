// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f2(a4) {
  return {
      ...a4,
      "x": a4,
      __proto__: a4,
  };
}
const v14 = f2(f2);
const o26 = {
    ...v14,
    "x": 2626,
};
f2(f2);

// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-externalize-string --shared-string-table

let o = {};
const key = "12345";
externalizeString(key);
let handler = {
  has(target, key) {
    return Reflect.has(target, key);
  }
}
let p = new Proxy(o, handler);
assertFalse(Reflect.has(p, key));

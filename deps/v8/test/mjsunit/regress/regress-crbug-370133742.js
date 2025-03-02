// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-generalization

for (var cu = 0; cu <= 0xffff; ++cu) {
  var Elimination =
    ((cu === 0x002A) || (cu === 0x002F) || (cu === 0x005C) || (cu === 0x002B) ||
     (cu === 0x003F) || (cu === 0x0028) || (cu === 0x0029) ||
     (cu === 0x005B) || (cu === 0x005D) || (cu === 0x007B) || (cu === 0x007D));
}

var obj = {};

Object.defineProperties(obj, {
  property: {
    writable: Math
  }
});

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class B { }
class C extends B {
  constructor() {
    let x = 0;
    switch (0) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
        x += this;
        break;
      case this:
    }
  }
}
assertThrows(() => { new C(); }, ReferenceError);

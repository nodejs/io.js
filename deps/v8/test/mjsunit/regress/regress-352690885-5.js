// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

for (let j = 0; j < 10; j++) {
  new class {
    constructor() {
      try {
        new class {
          [new class extends(() => {
             super.__proto__;
             return Object;
           })() {}()] = eval();
        }
        ();
      } catch (error) {
        console.log(error);
      }
    }
  }
  ();

  if (j == 8) {
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
    gc();
  }
}

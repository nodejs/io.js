// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

let arrow_function;
let value_inside_class;

class C {
  get x() {
    return 900;
  }
};

class D {
  get x() {
    return 800;
  }
};

for (let j = 0; j < 10; j++) {
  new class A extends D {
    constructor() {
      super();

      try {
        new class B extends C {
          [new class extends(() => {
             value_inside_class = super.x;

             arrow_function = (() => {
               return super.x;
             });

             return Array;
           })() {}()] = eval();
        }
        ();

      } catch (e) {
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

assertSame(value_inside_class, arrow_function());

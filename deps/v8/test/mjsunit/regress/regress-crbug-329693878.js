// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

{
  class Outer {
    #a() { return 'Outer'; }
    a() { return this.#a(); }
    test() {
      return class  {
        static #a() { return 'Inner'; }
        static a() { return this.#a(); }
        b = undefined();
      };
    }
  }
  const obj = new Outer;
  const C = obj.test();
  assertThrows(() => obj.a.call(new C));
}

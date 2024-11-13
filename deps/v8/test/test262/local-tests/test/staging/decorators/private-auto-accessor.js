// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*---
description: Test private auto-accessors.
features: [decorators]
---*/

(function TestUninitializedPrivateAccessor() {
  class C {
    accessor #x;
    assertFieldIsUndefined() {
      assert.sameValue(this.#x, undefined);
    }
    assertFieldMatchesValue(value) {
      assert.sameValue(this.#x, value);
    }
    setField(value) {
      this.#x = value;
    }
  }
  let c = new C();
  c.assertFieldIsUndefined();
  c.setField(42);
  c.assertFieldMatchesValue(42);
})();

(function TestInitializedPrivateAccessor() {
  class C {
    accessor #x = 5;
    assertFieldMatchesValue(value) {
      assert.sameValue(this.#x, value);
    }
    setField(value) {
      this.#x = value;
    }
  }
  let c = new C();
  c.assertFieldMatchesValue(5);
  c.setField(42);
  c.assertFieldMatchesValue(42);
})();

(function TestInitializedMultiplePrivateAccessor() {
  class C {
    accessor #x = 5;
    accessor #y = 42;
    assertFieldsMatcheValues(value_x, value_y) {
      assert.sameValue(this.#x, value_x);
      assert.sameValue(this.#y, value_y);
    }
  }
  let c = new C();
  c.assertFieldsMatcheValues(5, 42);
})();

(function TestThrowOnDuplicatedName() {
  // Auto-accessors will instantiate private getter and setter with the same
  // name, which shouldn't collide with other private variables.
  let assertThrowsSyntaxError = (code_string) => {
    assert.throws(SyntaxError, () => {eval(code_string)});
  };
  assertThrowsSyntaxError('class C { accessor #x = 5;  accessor #x = 42; }');
  assertThrowsSyntaxError('class C { accessor #x = 5; #x = 42; }');
  assertThrowsSyntaxError('class C { accessor #x = 5; get #x() {}; }');
  assertThrowsSyntaxError('class C { accessor #x = 5; set #x(value) {}; }');
  assertThrowsSyntaxError('class C { accessor #x = 5; #x() {}; }');
  assertThrowsSyntaxError('class C { accessor #x = 5; static #x = 42; }');
  assertThrowsSyntaxError('class C { static accessor #x = 5; #x = 42; }');
  assertThrowsSyntaxError(
      'class C { static accessor #x = 5; static #x = 42; }');
})();

(function TestUninitializedStaticPrivateAccessor() {
  class C {
    static #x;
    static assertFieldIsUndefined() {
      assert.sameValue(C.#x, undefined);
    }
    static assertFieldMatchesValue(value) {
      assert.sameValue(C.#x, value);
    }
    static setField(value) {
      C.#x = value;
    }
  }
  C.assertFieldIsUndefined();
  C.setField(42);
  C.assertFieldMatchesValue(42);
})();

(function TestInitializedStaticPrivateAccessor() {
  class C {
    static accessor #x = 5;
    static assertFieldMatchesValue(value) {
      assert.sameValue(C.#x, value);
    }
    static setField(value) {
      C.#x = value;
    }
  }
  C.assertFieldMatchesValue(5);
  C.setField(42);
  C.assertFieldMatchesValue(42);
})();

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

function foo() {
  let getter_call_count = 0;

  const obj = {};
  Object.defineProperty(obj, "prop_that_forces_dictionary_mode", {
    get() {
      // Make `obj` dictionary mode before returning.
      assertTrue(%HasFastProperties(this));
      this.x = 1;
      this.y = 2;
      delete this.x;
      delete this.y;
      assertFalse(%HasFastProperties(this));

      return 12;
    },
    enumerable: true
  });
  Object.defineProperty(obj, "prop_with_getter", {
    get() {
      getter_call_count++;
      return 42;
    },
    enumerable: true
  });

  assertTrue(%HasFastProperties(obj));

  // obj will start in fast mode, then:
  //
  //   1. prop_with_getter is accessed for the first variable
  //   2. own properties are walked for the rest parameter
  //   3. prop_that_forces_dictionary_mode forces obj to be dictionary mode
  //   4. the prop_with_getter property (now an accessor pair in the dictionary)
  //      should be skipped and the getter shouldn't be called.

  let {prop_with_getter, ...props_excluding_getter} = obj;

  assertFalse(%HasFastProperties(obj));
  assertEquals(42, prop_with_getter);
  assertEquals({prop_that_forces_dictionary_mode: 12}, props_excluding_getter);
  assertEquals(1, getter_call_count);
}

%EnsureFeedbackVectorForFunction(foo);
foo();

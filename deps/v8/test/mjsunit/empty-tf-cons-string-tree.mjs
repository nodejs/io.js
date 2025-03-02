// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --stack-size=150

const flat_string = "1234567890123";  // ConsString min length 13.

// TF may create cons strings with an empty 'first'. This tests that such
// strings are appropriately handled and flattened.
function CreateNestedTFConsString(level) {
  let result = %ConstructConsString("", flat_string);
  for (let i = 0; i < level; i++) {
    result = %ConstructConsString("", result);
  }
  return result;
}

const re = /1.3/;

{
  let subject = CreateNestedTFConsString(0);
  assertTrue(re.test(subject));  // Flattens.
  assertTrue(%StringIsFlat(subject));
  assertEquals(subject, flat_string);
}

{
  let subject = CreateNestedTFConsString(1);
  assertTrue(re.test(subject));  // Flattens.
  assertTrue(%StringIsFlat(subject));
  assertEquals(subject, flat_string);
}

{
  let subject = CreateNestedTFConsString(1000);
  assertTrue(re.test(subject));  // Flattens.
  assertTrue(%StringIsFlat(subject));
  assertEquals(subject, flat_string);
}

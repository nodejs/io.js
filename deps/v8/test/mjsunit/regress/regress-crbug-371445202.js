// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc --allow-natives-syntax --verify-heap

function foo() {
  // The bytecode should have an ObjectBoilerplateDescription where the value is
  // not necessarily an internalized string.
  return {value: "abc" + "def"};
}

// Make sure "abcdef" already exists as an internalized string.
%InternalizeString("abcdef");

// Get the string out of the object, which should be the same string that is in
// the ObjectBoilerplateDescription.
let string = foo().value;

// Internalizing this string should now create a ThinString.
%InternalizeString(string);

// Heap verification should be ok with this.
gc();

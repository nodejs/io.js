// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

class MyString {
 [Symbol.toPrimitive]() {
   return 'new';
  }
}
MyString.__proto__ = String.prototype;

assertFalse(%StringWrapperToPrimitiveProtector());

let x = new MyString('old');
assertEquals('got new', 'got ' + x);

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

class MyNotString {
 [Symbol.toPrimitive]() {
   return 'new';
  }
}

let x = new MyNotString();
assertEquals('got new', 'got ' + x);

// Setting the toPrimitive symbol in a class unrelated to string wrappers won't
// invalidate the protector.
assertTrue(%StringWrapperToPrimitiveProtector());

// Also creating normal string wrappers won't invalidate the protector.
new String("nothing");
assertTrue(%StringWrapperToPrimitiveProtector());

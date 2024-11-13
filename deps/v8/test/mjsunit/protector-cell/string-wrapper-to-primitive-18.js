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

let x = Reflect.construct(String, ["old"], MyNotString);

// The protector is invalidated when we create the instance.
assertFalse(%StringWrapperToPrimitiveProtector());

assertEquals('got new', 'got ' + x);

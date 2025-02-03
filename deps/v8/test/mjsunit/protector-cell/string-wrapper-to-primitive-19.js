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

let x1 = Reflect.construct(String, ["old"]);

// The protector is not invalidated when calling Reflect.construct without a
// separate new.target.
assertTrue(%StringWrapperToPrimitiveProtector());

assertEquals('got old', 'got ' + x1);

let x2 = Reflect.construct(MyNotString, ["old"], String);

// The protector is not invalidated when not constructing string wrappers.
assertTrue(%StringWrapperToPrimitiveProtector());

assertThrows(() => { 'throws ' + x2; });

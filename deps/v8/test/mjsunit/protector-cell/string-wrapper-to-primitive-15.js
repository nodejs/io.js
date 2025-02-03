// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%StringWrapperToPrimitiveProtector());

function MyString() {}
MyString.prototype[Symbol.toPrimitive] = function() {
  return 'new';
}
function MyMiddle() {}
MyString.__proto__ = MyMiddle.prototype;
MyMiddle.__proto__ = String.prototype;

assertFalse(%StringWrapperToPrimitiveProtector());

let x = new MyString('old');
assertEquals('got new', 'got ' + x);

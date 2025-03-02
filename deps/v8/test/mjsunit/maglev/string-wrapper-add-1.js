// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-always-turbofan

function add(a, b) {
  return a + b;
}
%PrepareFunctionForOptimization(add);

const string = 'first';
const stringWrapper = new String('second');

assertEquals('firstsecond', add(string, stringWrapper));
%OptimizeMaglevOnNextCall(add);

assertEquals('firstsecond', add(string, stringWrapper));
assertTrue(isMaglevved(add));

// Also passing the params the other way around is supported (both params are
// of type "string or string wrapper").

assertEquals('secondfirst', add(stringWrapper, string));
assertTrue(isMaglevved(add));

assertEquals('firstfirst', add(string, string));
assertTrue(isMaglevved(add));

assertEquals('secondsecond', add(stringWrapper, stringWrapper));
assertTrue(isMaglevved(add));

// Invalidate the protector.
stringWrapper.valueOf = () => 'third';

assertUnoptimized(add);
assertEquals('firstthird', add(string, stringWrapper));

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

function add(a, b) {
  return a + b;
}
%PrepareFunctionForOptimization(add);

const string = 'first';
const stringWrapper = new String('second');

assertEquals('firstsecond', add(string, stringWrapper));
%OptimizeFunctionOnNextCall(add);

assertEquals('firstsecond', add(string, stringWrapper));
assertOptimized(add);

// Also passing the params the other way around is supported (both params are
// of type "string or string wrapper").

assertEquals('secondfirst', add(stringWrapper, string));
assertOptimized(add);

assertEquals('firstfirst', add(string, string));
assertOptimized(add);

assertEquals('secondsecond', add(stringWrapper, stringWrapper));
assertOptimized(add);

// Pass a JSPrimitiveWrapper which is not a string wrapper.
assertEquals('first123', add(string, new Number(123)));
assertUnoptimized(add);

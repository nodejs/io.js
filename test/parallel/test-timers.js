// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');

const inputs = [
  undefined,
  null,
  true,
  false,
  '',
  [],
  {},
  NaN,
  +Infinity,
  -Infinity,
  (1.0 / 0.0),      // sanity check
  parseFloat('x'),  // NaN
  -10,
  -1,
  -0.5,
  -0.1,
  -0.0,
  0,
  0.0,
  0.1,
  0.5,
  1,
  1.0,
  2147483648,     // Browser behavior: timeouts > 2^31-1 run on next tick
  12345678901234,  // ditto
];

const timeouts = [];
const intervals = [];

inputs.forEach((value, index) => {
  setTimeout(() => {
    timeouts[index] = true;
  }, value);

  const handle = setInterval(() => {
    clearInterval(handle); // Disarm timer or we'll never finish
    intervals[index] = true;
  }, value);
});

// All values in inputs array coerce to 1 ms. Therefore, they should all run
// before a timer set here for 2 ms.

setTimeout(common.mustCall(() => {
  // Assert that all other timers have run
  inputs.forEach((value, index) => {
    assert(timeouts[index]);
    assert(intervals[index]);
  });
}), 2);

// Test 10 ms timeout separately.
setTimeout(common.mustCall(), 10);
setInterval(common.mustCall(function() { clearInterval(this); }), 10);

// Test no timeout separately
setTimeout(common.mustCall());
// eslint-disable-next-line no-restricted-syntax
setInterval(common.mustCall(function() { clearInterval(this); }));

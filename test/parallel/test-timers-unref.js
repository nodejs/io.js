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

let unref_interval = false;
let unref_timer = false;
let checks = 0;

const LONG_TIME = 10 * 1000;
const SHORT_TIME = 100;

const timer = setTimeout(() => {}, 10);
assert.strictEqual(timer.hasRef(), true);
// Should not throw.
timer.unref().ref().unref();
assert.strictEqual(timer.hasRef(), false);

setInterval(() => {}, 10).unref().ref().unref();

setInterval(common.mustNotCall('Interval should not fire'), LONG_TIME).unref();
setTimeout(common.mustNotCall('Timer should not fire'), LONG_TIME).unref();

const interval = setInterval(common.mustCall(() => {
  unref_interval = true;
  clearInterval(interval);
}), SHORT_TIME);
interval.unref();

setTimeout(common.mustCall(() => {
  unref_timer = true;
}), SHORT_TIME).unref();

const check_unref = setInterval(() => {
  if (checks > 5 || (unref_interval && unref_timer))
    clearInterval(check_unref);
  checks += 1;
}, 100);

{
  const timeout =
    setTimeout(common.mustCall(() => {
      timeout.unref();
    }), SHORT_TIME);
}

{
  // Should not timeout the test
  const timeout =
    setInterval(() => timeout.unref(), SHORT_TIME);
}

// Should not assert on args.This()->InternalFieldCount() > 0.
// See https://github.com/nodejs/node-v0.x-archive/issues/4261.
{
  const t = setInterval(() => {}, 1);
  process.nextTick(t.unref.bind({}));
  process.nextTick(t.unref.bind(t));
}

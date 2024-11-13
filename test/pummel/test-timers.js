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

const WINDOW = 200; // Why does this need to be so big?


{
  const starttime = Date.now();

  setTimeout(common.mustCall(function() {
    const endtime = Date.now();

    const diff = endtime - starttime;
    assert.ok(diff > 0);
    console.error(`diff: ${diff}`);

    assert.ok(Math.abs(diff - 1000) < WINDOW);
  }), 1000);
}

// This timer shouldn't execute
{
  const id = setTimeout(common.mustNotCall(), 500);
  clearTimeout(id);
}

{
  const starttime = Date.now();

  let interval_count = 0;

  setInterval(common.mustCall(function() {
    interval_count += 1;
    const endtime = Date.now();

    const diff = endtime - starttime;
    assert.ok(diff > 0);
    console.error(`diff: ${diff}`);

    const t = interval_count * 1000;

    assert.ok(Math.abs(diff - t) < WINDOW * interval_count);

    assert.ok(interval_count <= 3, `interval_count: ${interval_count}`);
    if (interval_count === 3)
      clearInterval(this);
  }, 3), 1000);
}


// Single param:
{
  setTimeout(function(param) {
    assert.strictEqual(param, 'test param');
  }, 1000, 'test param');
}

{
  let interval_count = 0;
  setInterval(function(param) {
    ++interval_count;
    assert.strictEqual(param, 'test param');

    if (interval_count === 3)
      clearInterval(this);
  }, 1000, 'test param');
}


// Multiple param
{
  setTimeout(function(param1, param2) {
    assert.strictEqual(param1, 'param1');
    assert.strictEqual(param2, 'param2');
  }, 1000, 'param1', 'param2');
}

{
  let interval_count = 0;
  setInterval(function(param1, param2) {
    ++interval_count;
    assert.strictEqual(param1, 'param1');
    assert.strictEqual(param2, 'param2');

    if (interval_count === 3)
      clearInterval(this);
  }, 1000, 'param1', 'param2');
}

// setInterval(cb, 0) should be called multiple times.
{
  let count = 0;
  const interval = setInterval(common.mustCall(function() {
    if (++count > 10) clearInterval(interval);
  }, 11), 0);
}

// We should be able to clearTimeout multiple times without breakage.
{
  const t = common.mustCall(3);

  setTimeout(t, 200);
  setTimeout(t, 200);
  const y = setTimeout(t, 200);

  clearTimeout(y);
  setTimeout(t, 200);
  clearTimeout(y);
}

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

// Check `id -G` and `process.getgroups()` return same groups.

if (common.isMacOS)
  common.skip('Output of `id -G` is unreliable on Darwin.');

const assert = require('assert');
const exec = require('child_process').exec;

if (typeof process.getgroups === 'function') {
  const groups = unique(process.getgroups());
  assert(Array.isArray(groups));
  assert(groups.length > 0);
  exec('id -G', function(err, stdout) {
    assert.ifError(err);
    const real_groups = unique(stdout.match(/\d+/g).map(Number));
    assert.deepStrictEqual(groups, real_groups);
    check(groups, real_groups);
    check(real_groups, groups);
  });
}

function check(a, b) {
  for (let i = 0; i < a.length; ++i) assert(b.includes(a[i]));
}

function unique(groups) {
  return [...new Set(groups)].sort();
}

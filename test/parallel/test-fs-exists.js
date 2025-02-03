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
const fs = require('fs');
const f = __filename;

assert.throws(() => fs.exists(f), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => fs.exists(), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => fs.exists(f, {}), { code: 'ERR_INVALID_ARG_TYPE' });

fs.exists(f, common.mustCall(function(y) {
  assert.strictEqual(y, true);
}));

fs.exists(`${f}-NO`, common.mustCall(function(y) {
  assert.strictEqual(y, false);
}));

// If the path is invalid, fs.exists will still invoke the callback with false
// instead of throwing errors
fs.exists(new URL('https://foo'), common.mustCall(function(y) {
  assert.strictEqual(y, false);
}));

fs.exists({}, common.mustCall(function(y) {
  assert.strictEqual(y, false);
}));

assert(fs.existsSync(f));
assert(!fs.existsSync(`${f}-NO`));

// fs.existsSync() never throws
const msg = 'Passing invalid argument types to fs.existsSync is deprecated';
common.expectWarning('DeprecationWarning', msg, 'DEP0187');
assert(!fs.existsSync());
assert(!fs.existsSync({}));
assert(!fs.existsSync(new URL('https://foo')));

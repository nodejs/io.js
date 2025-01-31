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
const fixtures = require('../common/fixtures');
const assert = require('assert');
const { builtinModules } = require('module');
const path = require('path');

assert.strictEqual(
  require.resolve(fixtures.path('a')).toLowerCase(),
  fixtures.path('a.js').toLowerCase());
assert.strictEqual(
  require.resolve(fixtures.path('nested-index', 'one')).toLowerCase(),
  fixtures.path('nested-index', 'one', 'index.js').toLowerCase());
assert.strictEqual(require.resolve('path'), 'path');

// Test configurable resolve() paths.
require(fixtures.path('require-resolve.js'));
require(fixtures.path('resolve-paths', 'default', 'verify-paths.js'));

[1, false, null, undefined, {}].forEach((value) => {
  const message = 'The "request" argument must be of type string.' +
    common.invalidArgTypeHelper(value);
  assert.throws(
    () => { require.resolve(value); },
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message
    });

  assert.throws(
    () => { require.resolve.paths(value); },
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message
    });
});

// Test require.resolve.paths.
{
  // builtinModules.
  builtinModules.forEach((mod) => {
    // TODO(@jasnell): Remove once node:quic is no longer flagged
    if (mod === 'node:quic') return;
    assert.strictEqual(require.resolve.paths(mod), null);
    if (!mod.startsWith('node:')) {
      assert.strictEqual(require.resolve.paths(`node:${mod}`), null);
    }
  });

  // node_modules.
  const resolvedPaths = require.resolve.paths('eslint');
  assert.strictEqual(Array.isArray(resolvedPaths), true);
  assert.strictEqual(resolvedPaths[0].includes('node_modules'), true);

  // relativeModules.
  const relativeModules = ['.', '..', './foo', '../bar'];
  relativeModules.forEach((mod) => {
    const resolvedPaths = require.resolve.paths(mod);
    assert.strictEqual(Array.isArray(resolvedPaths), true);
    assert.strictEqual(resolvedPaths.length, 1);
    assert.strictEqual(resolvedPaths[0], path.dirname(__filename));

    // Shouldn't look up relative modules from 'node_modules'.
    assert.strictEqual(resolvedPaths.includes('/node_modules'), false);
  });
}

{
  assert.strictEqual(require.resolve('node:test'), 'node:test');
  assert.strictEqual(require.resolve('node:fs'), 'node:fs');

  assert.throws(
    () => require.resolve('node:unknown'),
    { code: 'MODULE_NOT_FOUND' },
  );
}

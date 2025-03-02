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
require('../common');
const assert = require('assert');
const cluster = require('cluster');

assert(cluster.isPrimary);

// cluster.settings should not be initialized until needed
assert.deepStrictEqual(cluster.settings, {});

cluster.setupPrimary();
assert.deepStrictEqual(cluster.settings, {
  args: process.argv.slice(2),
  exec: process.argv[1],
  execArgv: process.execArgv,
  silent: false,
  schedulingPolicy: 2,
});
console.log('ok sets defaults');

cluster.setupPrimary({ exec: 'overridden' });
assert.strictEqual(cluster.settings.exec, 'overridden');
console.log('ok overrides defaults');

cluster.setupPrimary({ args: ['foo', 'bar'] });
assert.strictEqual(cluster.settings.exec, 'overridden');
assert.deepStrictEqual(cluster.settings.args, ['foo', 'bar']);

cluster.setupPrimary({ execArgv: ['baz', 'bang'] });
assert.strictEqual(cluster.settings.exec, 'overridden');
assert.deepStrictEqual(cluster.settings.args, ['foo', 'bar']);
assert.deepStrictEqual(cluster.settings.execArgv, ['baz', 'bang']);
console.log('ok preserves unchanged settings on repeated calls');

cluster.setupPrimary();
assert.deepStrictEqual(cluster.settings, {
  args: ['foo', 'bar'],
  exec: 'overridden',
  execArgv: ['baz', 'bang'],
  silent: false,
  schedulingPolicy: 2,
});
console.log('ok preserves current settings');

// When setters and getters were added for global.process and global.Buffer to
// create a deprecation path for them in ESM, this test was added to make sure
// the setters and getters behaved as expected.
// Ref: https://github.com/nodejs/node/pull/26882
// Ref: https://github.com/nodejs/node/pull/26334

'use strict';
require('../common');
const assert = require('assert');
const _process = require('process');
const { Buffer: _Buffer } = require('buffer');

assert.strictEqual(process, _process);
// eslint-disable-next-line no-global-assign
process = 'asdf';
assert.strictEqual(process, 'asdf');
assert.strictEqual(globalThis.process, 'asdf');
globalThis.process = _process;
assert.strictEqual(process, _process);
assert.strictEqual(
  typeof Object.getOwnPropertyDescriptor(globalThis, 'process').get,
  'function');

assert.strictEqual(Buffer, _Buffer);
// eslint-disable-next-line no-global-assign
Buffer = 'asdf';
assert.strictEqual(Buffer, 'asdf');
assert.strictEqual(globalThis.Buffer, 'asdf');
globalThis.Buffer = _Buffer;
assert.strictEqual(Buffer, _Buffer);
assert.strictEqual(
  typeof Object.getOwnPropertyDescriptor(globalThis, 'Buffer').get,
  'function');

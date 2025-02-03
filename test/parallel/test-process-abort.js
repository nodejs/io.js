'use strict';

const common = require('../common');
const assert = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.abort() is not available in Workers');
}

// Check that our built-in methods do not have a prototype/constructor behaviour
// if they don't need to. This could be tested for any of our C++ methods.
assert.strictEqual(process.abort.prototype, undefined);
assert.throws(() => new process.abort(), TypeError);

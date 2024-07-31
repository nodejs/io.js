'use strict';
const common = require('../../common');
const path = require('path');
const assert = require('assert');
const { Worker } = require('worker_threads');
const binding = path.resolve(__dirname, `./build/${common.buildType}/binding`);
const { getFreeCallCount } = require(binding);

// getFreeCallCount initial value is 0
assert.strictEqual(getFreeCallCount(), 0);

// Test that buffers allocated with a free callback through our APIs are
// released when a Worker owning it exits.

const w = new Worker(`require(${JSON.stringify(binding)})`, { eval: true });

w.on('exit', common.mustCall(() => {
  assert.strictEqual(getFreeCallCount(), 1);
}));

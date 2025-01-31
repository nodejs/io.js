'use strict';
const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

// Test that 'exit' is emitted if 'beforeExit' throws.

process.on('exit', common.mustCall(() => {
  process.exitCode = 0;
}));
process.on('beforeExit', common.mustCall(() => {
  throw new Error();
}));

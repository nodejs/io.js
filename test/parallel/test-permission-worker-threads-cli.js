// Flags: --permission --allow-fs-read=*
'use strict';

const common = require('../common');
const {
  Worker,
  isMainThread,
} = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');

// Guarantee the initial state
{
  assert.ok(!process.permission.has('worker'));
}

if (isMainThread) {
  assert.throws(() => {
    new Worker(__filename);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'WorkerThreads',
  }));
} else {
  assert.fail('it should not be called');
}

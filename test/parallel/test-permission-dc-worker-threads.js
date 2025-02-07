// Flags: --permission --allow-fs-read=* --experimental-test-module-mocks
'use strict';

const common = require('../common');
const assert = require('node:assert');

{
  const diagnostics_channel = require('node:diagnostics_channel');
  diagnostics_channel.subscribe('worker_threads', common.mustNotCall());
  const { mock } = require('node:test');

  // Module mocking should throw instead of posting to worker_threads dc
  assert.throws(() => {
    mock.module('node:path');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'WorkerThreads',
  }));
}

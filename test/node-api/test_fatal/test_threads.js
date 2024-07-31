'use strict';
const common = require('../../common');
const assert = require('assert');
const child_process = require('child_process');
const test_fatal = require(`./build/${common.buildType}/test_fatal`);

// Test in a child process because the test code will trigger a fatal error
// that crashes the process.
if (process.argv[2] === 'child') {
  test_fatal.TestThread();
  while (true) {
    // Busy loop to allow the work thread to abort.
  }
}

const p = child_process.spawnSync(
  process.execPath, [ __filename, 'child' ]);
assert.ifError(p.error);
assert.ok(p.stderr.toString().includes(
  'FATAL ERROR: work_thread foobar'));
assert(common.nodeProcessAborted(p.status, p.signal));

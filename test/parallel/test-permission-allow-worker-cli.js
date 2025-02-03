// Flags: --permission --allow-worker --allow-fs-read=*
'use strict';

require('../common');
const assert = require('assert');
const { isMainThread, Worker } = require('worker_threads');

if (!isMainThread) {
  process.exit(0);
}

// Guarantee the initial state
{
  assert.ok(process.permission.has('worker'));
}

// When a permission is set by cli, the process shouldn't be able
// to spawn unless --allow-worker is sent
{
  // doesNotThrow
  new Worker(__filename).on('exit', (code) => assert.strictEqual(code, 0));
}

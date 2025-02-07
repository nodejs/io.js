'use strict';

const common = require('../common');
const assert = require('assert');
const { spawnSync } = require('child_process');
const fixtures = require('../common/fixtures');
const file = fixtures.path('permission', 'inspector-brk.js');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

common.skipIfInspectorDisabled();

// See https://github.com/nodejs/node/issues/53385
{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read=*',
      '--inspect-brk',
      file,
    ],
  );

  assert.strictEqual(status, 1);
  assert.match(stderr.toString(), /Error: Access to this API has been restricted/);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--inspect-brk',
      '--eval',
      'console.log("Hi!")',
    ],
  );

  assert.strictEqual(status, 1);
  assert.match(stderr.toString(), /Error: Access to this API has been restricted/);
}

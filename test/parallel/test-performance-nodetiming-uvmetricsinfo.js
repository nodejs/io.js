'use strict';

const common = require('../common');
common.skipIfWorker();

const { spawnSync } = require('node:child_process');
const assert = require('node:assert');
const fixtures = require('../common/fixtures');

const file = fixtures.path('test-nodetiming-uvmetricsinfo.js');

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      file,
    ],
  );
  assert.strictEqual(status, 0, stderr.toString());
}

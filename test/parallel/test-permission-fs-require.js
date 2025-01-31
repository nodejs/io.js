// Flags: --permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const fixtures = require('../common/fixtures');

const assert = require('node:assert');
const { spawnSync } = require('node:child_process');

{
  const mainModule = fixtures.path('permission', 'main-module.js');
  const requiredModule = fixtures.path('permission', 'required-module.js');
  const { status, stdout, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', mainModule,
      '--allow-fs-read', requiredModule,
      mainModule,
    ]
  );

  assert.strictEqual(status, 0, stderr.toString());
  assert.strictEqual(stdout.toString(), 'ok\n');
}

{
  // When required module is not passed as allowed path
  const mainModule = fixtures.path('permission', 'main-module.js');
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', mainModule,
      mainModule,
    ]
  );

  assert.strictEqual(status, 1, stderr.toString());
  assert.match(stderr.toString(), /Error: Access to this API has been restricted/);
}

{
  // ESM loader test
  const mainModule = fixtures.path('permission', 'main-module.mjs');
  const requiredModule = fixtures.path('permission', 'required-module.mjs');
  const { status, stdout, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', mainModule,
      '--allow-fs-read', requiredModule,
      mainModule,
    ]
  );

  assert.strictEqual(status, 0, stderr.toString());
  assert.strictEqual(stdout.toString(), 'ok\n');
}

{
  // When required module is not passed as allowed path (ESM)
  const mainModule = fixtures.path('permission', 'main-module.mjs');
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', mainModule,
      mainModule,
    ]
  );

  assert.strictEqual(status, 1, stderr.toString());
  assert.match(stderr.toString(), /Error: Access to this API has been restricted/);
}

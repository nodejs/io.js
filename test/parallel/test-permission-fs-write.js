// Flags: --permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

if (!common.hasCrypto) {
  common.skip('no crypto');
}

const assert = require('assert');
const path = require('path');
const { spawnSync } = require('child_process');
const fixtures = require('../common/fixtures');

const blockedFolder = fixtures.path('permission', 'deny', 'protected-folder');
const blockedFile = fixtures.path('permission', 'deny', 'protected-file.md');
const relativeProtectedFile = './test/fixtures/permission/deny/protected-file.md';
const relativeProtectedFolder = './test/fixtures/permission/deny/protected-folder';

const commonPath = path.join(__filename, '../../common');
const regularFile = fixtures.path('permission', 'deny', 'regular-file.md');
const file = fixtures.path('permission', 'fs-write.js');

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read=*',
      `--allow-fs-write=${regularFile}`, `--allow-fs-write=${commonPath}`,
      file,
    ],
    {
      env: {
        ...process.env,
        BLOCKEDFILE: blockedFile,
        BLOCKEDFOLDER: blockedFolder,
        RELATIVEBLOCKEDFOLDER: relativeProtectedFolder,
        RELATIVEBLOCKEDFILE: relativeProtectedFile,
        ALLOWEDFILE: regularFile,
        ALLOWEDFOLDER: commonPath,
      },
    }
  );
  assert.strictEqual(status, 0, stderr.toString());
}

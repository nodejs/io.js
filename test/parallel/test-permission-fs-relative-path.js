// Flags: --permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const { spawnSync } = require('child_process');

{
  // Relative path as CLI args are supported
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', '*',
      '--allow-fs-write', '../fixtures/permission/deny/regular-file.md',
      '-e',
      `
      const path = require("path");
      const absolutePath = path.resolve("../fixtures/permission/deny/regular-file.md");
      console.log(process.permission.has("fs.write", absolutePath));
       `,
    ]
  );

  const [fsWrite] = stdout.toString().split('\n');
  assert.strictEqual(fsWrite, 'true');
  assert.strictEqual(status, 0);
}

// Flags: --permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
const path = require('path');
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
      '--allow-fs-write', path.resolve('../fixtures/permission/deny/regular-file.md'),
      '--allow-fs-write', path.resolve('../fixtures/permission/deny/regular-file.md'),
      '--allow-fs-read', path.resolve('../fixtures/permission/deny/regular-file.md'),
      '--allow-fs-read', path.resolve('../fixtures/permission/deny/regular-file.md'),
      '-e',
      `
      const path = require("path");
      const absolutePath = path.resolve("../fixtures/permission/deny/regular-file.md");
      const blockedPath = path.resolve("../fixtures/permission/deny/protected-file.md");
      console.log(process.permission.has("fs.write", absolutePath));
      console.log(process.permission.has("fs.read", absolutePath));
      console.log(process.permission.has("fs.read", blockedPath));
      console.log(process.permission.has("fs.write", blockedPath));
       `,
    ]
  );

  const [fsWrite, fsRead, fsBlockedRead, fsBlockedWrite] = stdout.toString().split('\n');
  assert.strictEqual(status, 0);
  assert.strictEqual(fsWrite, 'true');
  assert.strictEqual(fsRead, 'true');
  assert.strictEqual(fsBlockedRead, 'false');
  assert.strictEqual(fsBlockedWrite, 'false');
}

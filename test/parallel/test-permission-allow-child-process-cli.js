// Flags: --permission --allow-child-process --allow-fs-read=*
'use strict';

const common = require('../common');

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const childProcess = require('child_process');
const fs = require('fs');

if (process.argv[2] === 'child') {
  assert.throws(() => {
    fs.writeFileSync(__filename, 'should not write');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  process.exit(0);
}

// Guarantee the initial state
{
  assert.ok(process.permission.has('child'));
}

// When a permission is set by cli, the process shouldn't be able
// to spawn unless --allow-child-process is sent
{
  // doesNotThrow
  childProcess.spawnSync(process.execPath, ['--version']);
  childProcess.execSync(...common.escapePOSIXShell`"${process.execPath}" --version`);
  const child = childProcess.fork(__filename, ['child']);
  child.on('close', common.mustCall());
  childProcess.execFileSync(process.execPath, ['--version']);
}

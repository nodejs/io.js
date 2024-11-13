// Flags: --experimental-permission --allow-child-process --allow-fs-read=*
'use strict';

const common = require('../common');
common.skipIfWorker();
const assert = require('assert');
const childProcess = require('child_process');

if (process.argv[2] === 'child') {
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
  childProcess.fork(__filename, ['child']);
  childProcess.execFileSync(process.execPath, ['--version']);
}

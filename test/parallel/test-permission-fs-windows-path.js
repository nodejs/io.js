// Flags: --experimental-permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
common.skipIfWorker();

const assert = require('assert');
const { spawnSync } = require('child_process');

if (!common.isWindows) {
  common.skip('windows UNC path test');
}

{
  const { stdout, status } = spawnSync(process.execPath, [
    '--experimental-permission', '--allow-fs-write', 'C:\\\\', '-e',
    'console.log(process.permission.has("fs.write", "C:\\\\"))',
  ]);
  assert.strictEqual(stdout.toString(), 'true\n');
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--experimental-permission', '--allow-fs-write="\\\\?\\C:\\"', '-e',
    'console.log(process.permission.has("fs.write", "C:\\\\"))',
  ]);
  assert.strictEqual(stdout.toString(), 'false\n', stderr.toString());
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--experimental-permission', '--allow-fs-write', 'C:\\', '-e',
    `const path = require('path');
     console.log(process.permission.has('fs.write', path.toNamespacedPath('C:\\\\')))`,
  ]);
  assert.strictEqual(stdout.toString(), 'true\n', stderr.toString());
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--experimental-permission', '--allow-fs-write', 'C:\\*', '-e',
    "console.log(process.permission.has('fs.write', '\\\\\\\\A\\\\C:\\Users'))",
  ]);
  assert.strictEqual(stdout.toString(), 'false\n', stderr.toString());
  assert.strictEqual(status, 0);
}

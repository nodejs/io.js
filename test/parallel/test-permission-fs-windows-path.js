// Flags: --permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const { spawnSync } = require('child_process');

if (!common.isWindows) {
  common.skip('windows UNC path test');
}

{
  const { stdout, status } = spawnSync(process.execPath, [
    '--permission', '--allow-fs-write', 'C:\\\\', '-e',
    'console.log(process.permission.has("fs.write", "C:\\\\"))',
  ]);
  assert.strictEqual(stdout.toString(), 'true\n');
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--permission', '--allow-fs-write="\\\\?\\C:\\"', '-e',
    'console.log(process.permission.has("fs.write", "C:\\\\"))',
  ]);
  assert.strictEqual(stdout.toString(), 'false\n', stderr.toString());
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--permission', '--allow-fs-write', 'C:\\', '-e',
    `const path = require('path');
     console.log(process.permission.has('fs.write', path.toNamespacedPath('C:\\\\')))`,
  ]);
  assert.strictEqual(stdout.toString(), 'true\n', stderr.toString());
  assert.strictEqual(status, 0);
}

{
  const { stdout, status, stderr } = spawnSync(process.execPath, [
    '--permission', '--allow-fs-write', 'C:\\*', '-e',
    "console.log(process.permission.has('fs.write', '\\\\\\\\A\\\\C:\\Users'))",
  ]);
  assert.strictEqual(stdout.toString(), 'false\n', stderr.toString());
  assert.strictEqual(status, 0);
}

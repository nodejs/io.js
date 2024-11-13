'use strict';

require('../common');

const { spawnSync } = require('child_process');
const assert = require('assert');
const path = require('path');

{
  const tmpPath = path.resolve('./tmp/');
  const otherPath = path.resolve('./other-path/');
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-fs-write', tmpPath, '--allow-fs-write', otherPath, '-e',
      `console.log(process.permission.has("fs"));
      console.log(process.permission.has("fs.read"));
      console.log(process.permission.has("fs.write"));
      console.log(process.permission.has("fs.write", "./tmp/"));
      console.log(process.permission.has("fs.write", "./other-path/"));`,
    ]
  );
  const [fs, fsIn, fsOut, fsOutAllowed1, fsOutAllowed2] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'false');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(fsOutAllowed1, 'true');
  assert.strictEqual(fsOutAllowed2, 'true');
  assert.strictEqual(status, 0);
}

{
  const tmpPath = path.resolve('./tmp/');
  const pathWithComma = path.resolve('./other,path/');
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-fs-write',
      tmpPath,
      '--allow-fs-write',
      pathWithComma,
      '-e',
      `console.log(process.permission.has("fs"));
      console.log(process.permission.has("fs.read"));
      console.log(process.permission.has("fs.write"));
      console.log(process.permission.has("fs.write", "./tmp/"));
      console.log(process.permission.has("fs.write", "./other,path/"));`,
    ]
  );
  const [fs, fsIn, fsOut, fsOutAllowed1, fsOutAllowed2] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'false');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(fsOutAllowed1, 'true');
  assert.strictEqual(fsOutAllowed2, 'true');
  assert.strictEqual(status, 0);
}

{
  const filePath = path.resolve('./tmp/file,with,comma.txt');
  const { status, stdout, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      '--allow-fs-read=*',
      `--allow-fs-write=${filePath}`,
      '-e',
      `console.log(process.permission.has("fs"));
      console.log(process.permission.has("fs.read"));
      console.log(process.permission.has("fs.write"));
      console.log(process.permission.has("fs.write", "./tmp/file,with,comma.txt"));`,
    ]
  );
  const [fs, fsIn, fsOut, fsOutAllowed] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'true');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(fsOutAllowed, 'true');
  assert.strictEqual(status, 0);
  assert.ok(stderr.toString().includes('Warning: The --allow-fs-write CLI flag has changed.'));
}

'use strict';

const common = require('../common');

const fixtures = require('../common/fixtures');
const { spawnSync } = require('child_process');
const assert = require('assert');
const fs = require('fs');
const path = require('path');

{
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--permission', '-e',
      `console.log(process.permission.has("fs"));
       console.log(process.permission.has("fs.read"));
       console.log(process.permission.has("fs.write"));`,
    ]
  );

  const [fs, fsIn, fsOut] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'false');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(status, 0);
}

{
  const tmpPath = path.resolve('./tmp/');
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-write', tmpPath, '-e',
      `console.log(process.permission.has("fs"));
      console.log(process.permission.has("fs.read"));
      console.log(process.permission.has("fs.write"));
      console.log(process.permission.has("fs.write", "./tmp/"));`,
    ]
  );
  const [fs, fsIn, fsOut, fsOutAllowed] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'false');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(fsOutAllowed, 'true');
  assert.strictEqual(status, 0);
}

{
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-write', '*', '-e',
      `console.log(process.permission.has("fs"));
       console.log(process.permission.has("fs.read"));
       console.log(process.permission.has("fs.write"));`,
    ]
  );

  const [fs, fsIn, fsOut] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'false');
  assert.strictEqual(fsOut, 'true');
  assert.strictEqual(status, 0);
}

{
  const { status, stdout } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read', '*', '-e',
      `console.log(process.permission.has("fs"));
       console.log(process.permission.has("fs.read"));
       console.log(process.permission.has("fs.write"));`,
    ]
  );

  const [fs, fsIn, fsOut] = stdout.toString().split('\n');
  assert.strictEqual(fs, 'false');
  assert.strictEqual(fsIn, 'true');
  assert.strictEqual(fsOut, 'false');
  assert.strictEqual(status, 0);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-write=*', '-p',
      'fs.readFileSync(process.execPath)',
    ]
  );
  assert.ok(
    stderr.toString().includes('Access to this API has been restricted'),
    stderr);
  assert.strictEqual(status, 1);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '-p',
      'fs.readFileSync(process.execPath)',
    ]
  );
  assert.ok(
    stderr.toString().includes('Access to this API has been restricted'),
    stderr);
  assert.strictEqual(status, 1);
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      '--allow-fs-read=*', '-p',
      'fs.writeFileSync("policy-deny-example.md", "# test")',
    ]
  );
  assert.ok(
    stderr.toString().includes('Access to this API has been restricted'),
    stderr);
  assert.strictEqual(status, 1);
  assert.ok(!fs.existsSync('permission-deny-example.md'));
}

{
  const { root } = path.parse(process.cwd());
  const abs = (p) => path.join(root, p);
  const firstPath = abs(path.sep + process.cwd().split(path.sep, 2)[1]);
  if (firstPath.startsWith('/etc')) {
    common.skip('/etc as firstPath');
  }
  if (firstPath.startsWith('/tmp')) {
    common.skip('/tmp as firstPath');
  }
  const file = fixtures.path('permission', 'loader', 'index.js');
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${firstPath}`,
      file,
    ]
  );
  assert.match(stderr.toString(), /resource: '.*?[\\/](?:etc|passwd)'/);
  assert.strictEqual(status, 1);
}

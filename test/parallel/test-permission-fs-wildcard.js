// Flags: --experimental-permission --allow-fs-read=* --allow-child-process
'use strict';

const common = require('../common');
common.skipIfWorker();

const assert = require('assert');
const path = require('path');
const { spawnSync } = require('child_process');

const fixtures = require('../common/fixtures');

const commonPathWildcard = path.join(__filename, '../../common*');
const file = fixtures.path('permission', 'fs-wildcard.js');

let allowList = [];

if (common.isWindows) {
  const { root } = path.parse(process.cwd());
  const abs = (p) => path.join(root, p);
  allowList = [
    'tmp\\*',
    'example\\foo*',
    'example\\bar*',
    'folder\\*',
    'show',
    'slower',
    'slown',
    'home\\foo\\*',
  ].map(abs);
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      ...allowList.flatMap((path) => ['--allow-fs-read', path]),
      '-e',
      `
        const path = require('path');
        const assert = require('assert');
        const { root } = path.parse(process.cwd());
        const abs = (p) => path.join(root, p);
        assert.ok(!process.permission.has('fs.read', abs('slow')));
        assert.ok(!process.permission.has('fs.read', abs('slows')));
        assert.ok(process.permission.has('fs.read', abs('slown')));
        assert.ok(process.permission.has('fs.read', abs('home\\\\foo')));
        assert.ok(process.permission.has('fs.read', abs('home\\\\foo\\\\')));
        assert.ok(!process.permission.has('fs.read', abs('home\\\\fo')));
      `,
    ]
  );
  assert.strictEqual(status, 0, stderr.toString());
} else {
  allowList = [
    '/tmp/*',
    '/example/foo*',
    '/example/bar*',
    '/folder/*',
    '/show',
    '/slower',
    '/slown',
    '/home/foo/*',
    '/files/index.js',
    '/files/index.json',
    '/files/i',
  ];
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      ...allowList.flatMap((path) => ['--allow-fs-read', path]),
      '-e',
      `
        const assert = require('assert')
        assert.ok(!process.permission.has('fs.read', '/slow'));
        assert.ok(!process.permission.has('fs.read', '/slows'));
        assert.ok(process.permission.has('fs.read', '/slown'));
        assert.ok(process.permission.has('fs.read', '/home/foo'));
        assert.ok(process.permission.has('fs.read', '/home/foo/'));
        assert.ok(!process.permission.has('fs.read', '/home/fo'));
        assert.ok(process.permission.has('fs.read', '/files/index.js'));
        assert.ok(process.permission.has('fs.read', '/files/index.json'));
        assert.ok(!process.permission.has('fs.read', '/files/index.j'));
        assert.ok(process.permission.has('fs.read', '/files/i'));
      `,
    ]
  );
  assert.strictEqual(status, 0, stderr.toString());
}

{
  const { status, stderr } = spawnSync(
    process.execPath,
    [
      '--experimental-permission',
      `--allow-fs-read=${file}`, `--allow-fs-read=${commonPathWildcard}`, ...allowList.flatMap((path) => ['--allow-fs-read', path]),
      file,
    ],
  );
  assert.strictEqual(status, 0, stderr.toString());
}

{
  if (!common.isWindows) {
    const { status, stderr } = spawnSync(
      process.execPath,
      [
        '--experimental-permission',
        '--allow-fs-read=/a/b/*',
        '--allow-fs-read=/a/b/d',
        '--allow-fs-read=/etc/passwd.*',
        '--allow-fs-read=/home/*.js',
        '-e',
        `
        const assert = require('assert')
        assert.ok(process.permission.has('fs.read', '/a/b/c'));
        assert.ok(!process.permission.has('fs.read', '/a/c/c'));
        assert.ok(!process.permission.has('fs.read', '/etc/passwd'));
        assert.ok(process.permission.has('fs.read', '/home/another-file.md'));
      `,
      ]
    );
    assert.strictEqual(status, 0, stderr.toString());
  }
}

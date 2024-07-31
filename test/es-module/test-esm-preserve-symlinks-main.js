'use strict';

const { spawnPromisified, skip } = require('../common');
const tmpdir = require('../common/tmpdir');

// Invoke the main file via a symlink.  In this case --preserve-symlinks-main
// dictates that it'll resolve relative imports in the main file relative to
// the symlink, and not relative to the symlink target; the file structure set
// up below requires this to not crash when loading ./submodule_link.js

const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { execPath } = require('node:process');
const { describe, it } = require('node:test');

tmpdir.refresh();
const tmpDir = tmpdir.path;

fs.mkdirSync(path.join(tmpDir, 'nested'));
fs.mkdirSync(path.join(tmpDir, 'nested2'));

const entry = path.join(tmpDir, 'nested', 'entry.js');
const entry_link_absolute_path = path.join(tmpDir, 'index.js');
const submodule = path.join(tmpDir, 'nested2', 'submodule.js');
const submodule_link_absolute_path = path.join(tmpDir, 'submodule_link.js');

fs.writeFileSync(entry, `
const assert = require('assert');

// this import only resolves with --preserve-symlinks-main set
require('./submodule_link.js');
`);
fs.writeFileSync(submodule, '');

try {
  fs.symlinkSync(entry, entry_link_absolute_path);
  fs.symlinkSync(submodule, submodule_link_absolute_path);
} catch (err) {
  if (err.code !== 'EPERM') throw err;
  skip('insufficient privileges for symlinks');
}

describe('Invoke the main file via a symlink.', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should resolve relative imports in the main file', async () => {
    const { code } = await spawnPromisified(execPath, [
      '--preserve-symlinks',
      '--preserve-symlinks-main',
      entry_link_absolute_path,
    ]);

    assert.strictEqual(code, 0);
  });

  it('should resolve relative imports in the main file when file extension is omitted', async () => {
    const entry_link_absolute_path_without_ext = path.join(tmpDir, 'index');

    const { code } = await spawnPromisified(execPath, [
      '--preserve-symlinks',
      '--preserve-symlinks-main',
      entry_link_absolute_path_without_ext,
    ]);

    assert.strictEqual(code, 0);
  });

  it('should resolve relative imports in the main file when filename(index.js) is omitted', async () => {
    const { code } = await spawnPromisified(execPath, [
      '--preserve-symlinks',
      '--preserve-symlinks-main',
      tmpDir,
    ]);

    assert.strictEqual(code, 0);
  });
});

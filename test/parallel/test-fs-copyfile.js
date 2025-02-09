// Flags: --expose-internals
'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const fs = require('fs');
const { internalBinding } = require('internal/test/binding');
const { getValidMode } = require('internal/fs/utils');
const {
  UV_ENOENT,
  UV_EEXIST
} = internalBinding('uv');
const src = fixtures.path('a.js');
const dest = tmpdir.resolve('copyfile.out');
const {
  COPYFILE_EXCL,
  COPYFILE_FICLONE,
  COPYFILE_FICLONE_FORCE,
  UV_FS_COPYFILE_EXCL,
  UV_FS_COPYFILE_FICLONE,
  UV_FS_COPYFILE_FICLONE_FORCE
} = fs.constants;

function verify(src, dest) {
  const srcData = fs.readFileSync(src, 'utf8');
  const srcStat = fs.statSync(src);
  const destData = fs.readFileSync(dest, 'utf8');
  const destStat = fs.statSync(dest);

  assert.strictEqual(srcData, destData);
  assert.strictEqual(srcStat.mode, destStat.mode);
  assert.strictEqual(srcStat.size, destStat.size);
}

tmpdir.refresh();

// Verify that flags are defined.
assert.strictEqual(typeof COPYFILE_EXCL, 'number');
assert.strictEqual(typeof COPYFILE_FICLONE, 'number');
assert.strictEqual(typeof COPYFILE_FICLONE_FORCE, 'number');
assert.strictEqual(typeof UV_FS_COPYFILE_EXCL, 'number');
assert.strictEqual(typeof UV_FS_COPYFILE_FICLONE, 'number');
assert.strictEqual(typeof UV_FS_COPYFILE_FICLONE_FORCE, 'number');
assert.strictEqual(COPYFILE_EXCL, UV_FS_COPYFILE_EXCL);
assert.strictEqual(COPYFILE_FICLONE, UV_FS_COPYFILE_FICLONE);
assert.strictEqual(COPYFILE_FICLONE_FORCE, UV_FS_COPYFILE_FICLONE_FORCE);

// Verify that files are overwritten when no flags are provided.
fs.writeFileSync(dest, '', 'utf8');
const result = fs.copyFileSync(src, dest);
assert.strictEqual(result, undefined);
verify(src, dest);

// Verify that files are overwritten with default flags.
fs.copyFileSync(src, dest, 0);
verify(src, dest);

// Verify that UV_FS_COPYFILE_FICLONE can be used.
fs.unlinkSync(dest);
fs.copyFileSync(src, dest, UV_FS_COPYFILE_FICLONE);
verify(src, dest);

// Verify that default mode is COPYFILE_FICLONE.
assert.strictEqual(getValidMode(null, 'copyFile'), COPYFILE_FICLONE);

// Verify that COPYFILE_FICLONE_FORCE can be used.
try {
  fs.unlinkSync(dest);
  fs.copyFileSync(src, dest, COPYFILE_FICLONE_FORCE);
  verify(src, dest);
} catch (err) {
  assert.strictEqual(err.syscall, 'copyfile');
  assert(err.code === 'ENOTSUP' || err.code === 'ENOTTY' ||
    err.code === 'ENOSYS' || err.code === 'EXDEV');
  assert.strictEqual(err.path, src);
  assert.strictEqual(err.dest, dest);
}

// Copies asynchronously.
tmpdir.refresh(); // Don't use unlinkSync() since the last test may fail.
fs.copyFile(src, dest, common.mustSucceed(() => {
  verify(src, dest);

  // Copy asynchronously with flags.
  fs.copyFile(src, dest, COPYFILE_EXCL, common.mustCall((err) => {
    if (err.code === 'ENOENT') {  // Could be ENOENT or EEXIST
      assert.strictEqual(err.message,
                         'ENOENT: no such file or directory, copyfile ' +
                         `'${src}' -> '${dest}'`);
      assert.strictEqual(err.errno, UV_ENOENT);
      assert.strictEqual(err.code, 'ENOENT');
      assert.strictEqual(err.syscall, 'copyfile');
    } else {
      assert.strictEqual(err.message,
                         'EEXIST: file already exists, copyfile ' +
                         `'${src}' -> '${dest}'`);
      assert.strictEqual(err.errno, UV_EEXIST);
      assert.strictEqual(err.code, 'EEXIST');
      assert.strictEqual(err.syscall, 'copyfile');
    }
  }));
}));

// Throws if callback is not a function.
assert.throws(() => {
  fs.copyFile(src, dest, 0, 0);
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});

// Throws if the source path is not a string.
[false, 1, {}, [], null, undefined].forEach((i) => {
  assert.throws(
    () => fs.copyFile(i, dest, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /src/
    }
  );
  assert.throws(
    () => fs.copyFile(src, i, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /dest/
    }
  );
  assert.throws(
    () => fs.copyFileSync(i, dest),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /src/
    }
  );
  assert.throws(
    () => fs.copyFileSync(src, i),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /dest/
    }
  );
});

assert.throws(() => {
  fs.copyFileSync(src, dest, 'r');
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError',
  message: /mode/
});

assert.throws(() => {
  fs.copyFileSync(src, dest, 8);
}, {
  code: 'ERR_OUT_OF_RANGE',
  name: 'RangeError',
});

assert.throws(() => {
  fs.copyFile(src, dest, 'r', common.mustNotCall());
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError',
  message: /mode/
});

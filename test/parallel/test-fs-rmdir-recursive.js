// Flags: --expose-internals
'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { validateRmdirOptions } = require('internal/fs/utils');

common.expectWarning(
  'DeprecationWarning',
  'In future versions of Node.js, fs.rmdir(path, { recursive: true }) ' +
      'will be removed. Use fs.rm(path, { recursive: true }) instead',
  'DEP0147'
);

tmpdir.refresh();

let count = 0;
const nextDirPath = (name = 'rmdir-recursive') =>
  tmpdir.resolve(`${name}-${count++}`);

function makeNonEmptyDirectory(depth, files, folders, dirname, createSymLinks) {
  fs.mkdirSync(dirname, { recursive: true });
  fs.writeFileSync(path.join(dirname, 'text.txt'), 'hello', 'utf8');

  const options = { flag: 'wx' };

  for (let f = files; f > 0; f--) {
    fs.writeFileSync(path.join(dirname, `f-${depth}-${f}`), '', options);
  }

  if (createSymLinks) {
    // Valid symlink
    fs.symlinkSync(
      `f-${depth}-1`,
      path.join(dirname, `link-${depth}-good`),
      'file'
    );

    // Invalid symlink
    fs.symlinkSync(
      'does-not-exist',
      path.join(dirname, `link-${depth}-bad`),
      'file'
    );
  }

  // File with a name that looks like a glob
  fs.writeFileSync(path.join(dirname, '[a-z0-9].txt'), '', options);

  depth--;
  if (depth <= 0) {
    return;
  }

  for (let f = folders; f > 0; f--) {
    fs.mkdirSync(
      path.join(dirname, `folder-${depth}-${f}`),
      { recursive: true }
    );
    makeNonEmptyDirectory(
      depth,
      files,
      folders,
      path.join(dirname, `d-${depth}-${f}`),
      createSymLinks
    );
  }
}

function removeAsync(dir) {
  // Removal should fail without the recursive option.
  fs.rmdir(dir, common.mustCall((err) => {
    assert.strictEqual(err.syscall, 'rmdir');

    // Removal should fail without the recursive option set to true.
    fs.rmdir(dir, { recursive: false }, common.mustCall((err) => {
      assert.strictEqual(err.syscall, 'rmdir');

      // Recursive removal should succeed.
      fs.rmdir(dir, { recursive: true }, common.mustSucceed(() => {
        // An error should occur if recursive and the directory does not exist.
        fs.rmdir(dir, { recursive: true }, common.mustCall((err) => {
          assert.strictEqual(err.code, 'ENOENT');
          // Attempted removal should fail now because the directory is gone.
          fs.rmdir(dir, common.mustCall((err) => {
            assert.strictEqual(err.syscall, 'rmdir');
          }));
        }));
      }));
    }));
  }));
}

// Test the asynchronous version
{
  // Create a 4-level folder hierarchy including symlinks
  let dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);
  removeAsync(dir);

  // Create a 2-level folder hierarchy without symlinks
  dir = nextDirPath();
  makeNonEmptyDirectory(2, 10, 2, dir, false);
  removeAsync(dir);

  // Create a flat folder including symlinks
  dir = nextDirPath();
  makeNonEmptyDirectory(1, 10, 2, dir, true);
  removeAsync(dir);
}

// Test the synchronous version.
{
  const dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);

  // Removal should fail without the recursive option set to true.
  assert.throws(() => {
    fs.rmdirSync(dir);
  }, { syscall: 'rmdir' });
  assert.throws(() => {
    fs.rmdirSync(dir, { recursive: false });
  }, { syscall: 'rmdir' });

  // Recursive removal should succeed.
  fs.rmdirSync(dir, { recursive: true });

  // An error should occur if recursive and the directory does not exist.
  assert.throws(() => fs.rmdirSync(dir, { recursive: true }),
                { code: 'ENOENT' });

  // Attempted removal should fail now because the directory is gone.
  assert.throws(() => fs.rmdirSync(dir), { syscall: 'rmdir' });
}

// Test the Promises based version.
(async () => {
  const dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);

  // Removal should fail without the recursive option set to true.
  await assert.rejects(fs.promises.rmdir(dir), { syscall: 'rmdir' });
  await assert.rejects(fs.promises.rmdir(dir, { recursive: false }), {
    syscall: 'rmdir'
  });

  // Recursive removal should succeed.
  await fs.promises.rmdir(dir, { recursive: true });

  // An error should occur if recursive and the directory does not exist.
  await assert.rejects(fs.promises.rmdir(dir, { recursive: true }),
                       { code: 'ENOENT' });

  // Attempted removal should fail now because the directory is gone.
  await assert.rejects(fs.promises.rmdir(dir), { syscall: 'rmdir' });
})().then(common.mustCall());

// Test input validation.
{
  const defaults = {
    retryDelay: 100,
    maxRetries: 0,
    recursive: false
  };
  const modified = {
    retryDelay: 953,
    maxRetries: 5,
    recursive: true
  };

  assert.deepStrictEqual(validateRmdirOptions(), defaults);
  assert.deepStrictEqual(validateRmdirOptions({}), defaults);
  assert.deepStrictEqual(validateRmdirOptions(modified), modified);
  assert.deepStrictEqual(validateRmdirOptions({
    maxRetries: 99
  }), {
    retryDelay: 100,
    maxRetries: 99,
    recursive: false
  });

  [null, 'foo', 5, NaN].forEach((bad) => {
    assert.throws(() => {
      validateRmdirOptions(bad);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "options" argument must be of type object\./
    });
  });

  [undefined, null, 'foo', Infinity, function() {}].forEach((bad) => {
    assert.throws(() => {
      validateRmdirOptions({ recursive: bad });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "options\.recursive" property must be of type boolean\./
    });
  });

  assert.throws(() => {
    validateRmdirOptions({ retryDelay: -1 });
  }, {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: /^The value of "options\.retryDelay" is out of range\./
  });

  assert.throws(() => {
    validateRmdirOptions({ maxRetries: -1 });
  }, {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: /^The value of "options\.maxRetries" is out of range\./
  });
}

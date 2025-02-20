// Flags: --expose-internals
'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');
const { execSync } = require('child_process');

const { validateRmOptionsSync } = require('internal/fs/utils');

tmpdir.refresh();

let count = 0;
const nextDirPath = (name = 'rm') =>
  tmpdir.resolve(`${name}-${count++}`);

const isGitPresent = (() => {
  try { execSync('git --version'); return true; } catch { return false; }
})();

function gitInit(gitDirectory) {
  fs.mkdirSync(gitDirectory);
  execSync('git init', common.mustNotMutateObjectDeep({ cwd: gitDirectory }));
}

function makeNonEmptyDirectory(depth, files, folders, dirname, createSymLinks) {
  fs.mkdirSync(dirname, common.mustNotMutateObjectDeep({ recursive: true }));
  fs.writeFileSync(path.join(dirname, 'text.txt'), 'hello', 'utf8');

  const options = common.mustNotMutateObjectDeep({ flag: 'wx' });

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

    // Symlinks that form a loop
    [['a', 'b'], ['b', 'a']].forEach(([x, y]) => {
      fs.symlinkSync(
        `link-${depth}-loop-${x}`,
        path.join(dirname, `link-${depth}-loop-${y}`),
        'file'
      );
    });
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
  fs.rm(dir, common.mustCall((err) => {
    assert.strictEqual(err.syscall, 'rm');

    // Removal should fail without the recursive option set to true.
    fs.rm(dir, common.mustNotMutateObjectDeep({ recursive: false }), common.mustCall((err) => {
      assert.strictEqual(err.syscall, 'rm');

      // Recursive removal should succeed.
      fs.rm(dir, common.mustNotMutateObjectDeep({ recursive: true }), common.mustSucceed(() => {

        // Attempted removal should fail now because the directory is gone.
        fs.rm(dir, common.mustCall((err) => {
          assert.strictEqual(err.syscall, 'lstat');
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

  // Same test using URL instead of a path
  dir = nextDirPath();
  makeNonEmptyDirectory(2, 10, 2, dir, false);
  removeAsync(pathToFileURL(dir));

  // Create a flat folder including symlinks
  dir = nextDirPath();
  makeNonEmptyDirectory(1, 10, 2, dir, true);
  removeAsync(dir);

  // Should fail if target does not exist
  fs.rm(
    tmpdir.resolve('noexist.txt'),
    common.mustNotMutateObjectDeep({ recursive: true }),
    common.mustCall((err) => {
      assert.strictEqual(err.code, 'ENOENT');
    })
  );

  // Should delete a file
  const filePath = tmpdir.resolve('rm-async-file.txt');
  fs.writeFileSync(filePath, '');
  fs.rm(filePath, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    try {
      assert.strictEqual(err, null);
      assert.strictEqual(fs.existsSync(filePath), false);
    } finally {
      fs.rmSync(filePath, common.mustNotMutateObjectDeep({ force: true }));
    }
  }));

  // Should delete a valid symlink
  const linkTarget = tmpdir.resolve('link-target-async.txt');
  fs.writeFileSync(linkTarget, '');
  const validLink = tmpdir.resolve('valid-link-async');
  fs.symlinkSync(linkTarget, validLink);
  fs.rm(validLink, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    try {
      assert.strictEqual(err, null);
      assert.strictEqual(fs.existsSync(validLink), false);
    } finally {
      fs.rmSync(linkTarget, common.mustNotMutateObjectDeep({ force: true }));
      fs.rmSync(validLink, common.mustNotMutateObjectDeep({ force: true }));
    }
  }));

  // Should delete an invalid symlink
  const invalidLink = tmpdir.resolve('invalid-link-async');
  fs.symlinkSync('definitely-does-not-exist-async', invalidLink);
  fs.rm(invalidLink, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    try {
      assert.strictEqual(err, null);
      assert.strictEqual(fs.existsSync(invalidLink), false);
    } finally {
      fs.rmSync(invalidLink, common.mustNotMutateObjectDeep({ force: true }));
    }
  }));

  // Should delete a symlink that is part of a loop
  const loopLinkA = tmpdir.resolve('loop-link-async-a');
  const loopLinkB = tmpdir.resolve('loop-link-async-b');
  fs.symlinkSync(loopLinkA, loopLinkB);
  fs.symlinkSync(loopLinkB, loopLinkA);
  fs.rm(loopLinkA, common.mustNotMutateObjectDeep({ recursive: true }), common.mustCall((err) => {
    try {
      assert.strictEqual(err, null);
      assert.strictEqual(fs.existsSync(loopLinkA), false);
    } finally {
      fs.rmSync(loopLinkA, common.mustNotMutateObjectDeep({ force: true }));
      fs.rmSync(loopLinkB, common.mustNotMutateObjectDeep({ force: true }));
    }
  }));
}

// Removing a .git directory should not throw an EPERM.
// Refs: https://github.com/isaacs/rimraf/issues/21.
if (isGitPresent) {
  const gitDirectory = nextDirPath();
  gitInit(gitDirectory);
  fs.rm(gitDirectory, common.mustNotMutateObjectDeep({ recursive: true }), common.mustSucceed(() => {
    assert.strictEqual(fs.existsSync(gitDirectory), false);
  }));
}

// Test the synchronous version.
{
  const dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);

  // Removal should fail without the recursive option set to true.
  assert.throws(() => {
    fs.rmSync(dir);
  }, { syscall: 'rm' });
  assert.throws(() => {
    fs.rmSync(dir, common.mustNotMutateObjectDeep({ recursive: false }));
  }, { syscall: 'rm' });

  // Should fail if target does not exist
  assert.throws(() => {
    fs.rmSync(tmpdir.resolve('noexist.txt'), common.mustNotMutateObjectDeep({ recursive: true }));
  }, {
    code: 'ENOENT',
    name: 'Error',
    message: /^ENOENT: no such file or directory, lstat/
  });

  // Should delete a file
  const filePath = tmpdir.resolve('rm-file.txt');
  fs.writeFileSync(filePath, '');

  try {
    fs.rmSync(filePath, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(filePath), false);
  } finally {
    fs.rmSync(filePath, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete a valid symlink
  const linkTarget = tmpdir.resolve('link-target.txt');
  fs.writeFileSync(linkTarget, '');
  const validLink = tmpdir.resolve('valid-link');
  fs.symlinkSync(linkTarget, validLink);
  try {
    fs.rmSync(validLink);
    assert.strictEqual(fs.existsSync(validLink), false);
  } finally {
    fs.rmSync(linkTarget, common.mustNotMutateObjectDeep({ force: true }));
    fs.rmSync(validLink, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete an invalid symlink
  const invalidLink = tmpdir.resolve('invalid-link');
  fs.symlinkSync('definitely-does-not-exist', invalidLink);
  try {
    fs.rmSync(invalidLink);
    assert.strictEqual(fs.existsSync(invalidLink), false);
  } finally {
    fs.rmSync(invalidLink, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete a symlink that is part of a loop
  const loopLinkA = tmpdir.resolve('loop-link-a');
  const loopLinkB = tmpdir.resolve('loop-link-b');
  fs.symlinkSync(loopLinkA, loopLinkB);
  fs.symlinkSync(loopLinkB, loopLinkA);
  try {
    fs.rmSync(loopLinkA);
    assert.strictEqual(fs.existsSync(loopLinkA), false);
  } finally {
    fs.rmSync(loopLinkA, common.mustNotMutateObjectDeep({ force: true }));
    fs.rmSync(loopLinkB, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should accept URL
  const fileURL = tmpdir.fileURL('rm-file.txt');
  fs.writeFileSync(fileURL, '');

  try {
    fs.rmSync(fileURL, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(fileURL), false);
  } finally {
    fs.rmSync(fileURL, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Recursive removal should succeed.
  fs.rmSync(dir, { recursive: true });
  assert.strictEqual(fs.existsSync(dir), false);

  // Attempted removal should fail now because the directory is gone.
  assert.throws(() => fs.rmSync(dir), { syscall: 'lstat' });
}

// Removing a .git directory should not throw an EPERM.
// Refs: https://github.com/isaacs/rimraf/issues/21.
if (isGitPresent) {
  const gitDirectory = nextDirPath();
  gitInit(gitDirectory);
  fs.rmSync(gitDirectory, common.mustNotMutateObjectDeep({ recursive: true }));
  assert.strictEqual(fs.existsSync(gitDirectory), false);
}

// Test the Promises based version.
(async () => {
  const dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);

  // Removal should fail without the recursive option set to true.
  await assert.rejects(fs.promises.rm(dir), { syscall: 'rm' });
  await assert.rejects(fs.promises.rm(dir, common.mustNotMutateObjectDeep({ recursive: false })), {
    syscall: 'rm'
  });

  // Recursive removal should succeed.
  await fs.promises.rm(dir, common.mustNotMutateObjectDeep({ recursive: true }));
  assert.strictEqual(fs.existsSync(dir), false);

  // Attempted removal should fail now because the directory is gone.
  await assert.rejects(fs.promises.rm(dir), { syscall: 'lstat' });

  // Should fail if target does not exist
  await assert.rejects(fs.promises.rm(
    tmpdir.resolve('noexist.txt'),
    { recursive: true }
  ), {
    code: 'ENOENT',
    name: 'Error',
    message: /^ENOENT: no such file or directory, lstat/
  });

  // Should not fail if target does not exist and force option is true
  await fs.promises.rm(tmpdir.resolve('noexist.txt'), common.mustNotMutateObjectDeep({ force: true }));

  // Should delete file
  const filePath = tmpdir.resolve('rm-promises-file.txt');
  fs.writeFileSync(filePath, '');

  try {
    await fs.promises.rm(filePath, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(filePath), false);
  } finally {
    fs.rmSync(filePath, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete a valid symlink
  const linkTarget = tmpdir.resolve('link-target-prom.txt');
  fs.writeFileSync(linkTarget, '');
  const validLink = tmpdir.resolve('valid-link-prom');
  fs.symlinkSync(linkTarget, validLink);
  try {
    await fs.promises.rm(validLink);
    assert.strictEqual(fs.existsSync(validLink), false);
  } finally {
    fs.rmSync(linkTarget, common.mustNotMutateObjectDeep({ force: true }));
    fs.rmSync(validLink, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete an invalid symlink
  const invalidLink = tmpdir.resolve('invalid-link-prom');
  fs.symlinkSync('definitely-does-not-exist-prom', invalidLink);
  try {
    await fs.promises.rm(invalidLink);
    assert.strictEqual(fs.existsSync(invalidLink), false);
  } finally {
    fs.rmSync(invalidLink, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should delete a symlink that is part of a loop
  const loopLinkA = tmpdir.resolve('loop-link-prom-a');
  const loopLinkB = tmpdir.resolve('loop-link-prom-b');
  fs.symlinkSync(loopLinkA, loopLinkB);
  fs.symlinkSync(loopLinkB, loopLinkA);
  try {
    await fs.promises.rm(loopLinkA);
    assert.strictEqual(fs.existsSync(loopLinkA), false);
  } finally {
    fs.rmSync(loopLinkA, common.mustNotMutateObjectDeep({ force: true }));
    fs.rmSync(loopLinkB, common.mustNotMutateObjectDeep({ force: true }));
  }

  // Should accept URL
  const fileURL = tmpdir.fileURL('rm-promises-file.txt');
  fs.writeFileSync(fileURL, '');

  try {
    await fs.promises.rm(fileURL, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(fileURL), false);
  } finally {
    fs.rmSync(fileURL, common.mustNotMutateObjectDeep({ force: true }));
  }
})().then(common.mustCall());

// Removing a .git directory should not throw an EPERM.
// Refs: https://github.com/isaacs/rimraf/issues/21.
if (isGitPresent) {
  (async () => {
    const gitDirectory = nextDirPath();
    gitInit(gitDirectory);
    await fs.promises.rm(gitDirectory, common.mustNotMutateObjectDeep({ recursive: true }));
    assert.strictEqual(fs.existsSync(gitDirectory), false);
  })().then(common.mustCall());
}

// Test input validation.
{
  const dir = nextDirPath();
  makeNonEmptyDirectory(4, 10, 2, dir, true);
  const filePath = (tmpdir.resolve('rm-args-file.txt'));
  fs.writeFileSync(filePath, '');

  const defaults = {
    retryDelay: 100,
    maxRetries: 0,
    recursive: false,
    force: false
  };
  const modified = {
    retryDelay: 953,
    maxRetries: 5,
    recursive: true,
    force: false
  };

  assert.deepStrictEqual(validateRmOptionsSync(filePath), defaults);
  assert.deepStrictEqual(validateRmOptionsSync(filePath, {}), defaults);
  assert.deepStrictEqual(validateRmOptionsSync(filePath, modified), modified);
  assert.deepStrictEqual(validateRmOptionsSync(filePath, {
    maxRetries: 99
  }), {
    retryDelay: 100,
    maxRetries: 99,
    recursive: false,
    force: false
  });

  [null, 'foo', 5, NaN].forEach((bad) => {
    assert.throws(() => {
      validateRmOptionsSync(filePath, bad);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "options" argument must be of type object\./
    });
  });

  [undefined, null, 'foo', Infinity, function() {}].forEach((bad) => {
    assert.throws(() => {
      validateRmOptionsSync(filePath, { recursive: bad });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "options\.recursive" property must be of type boolean\./
    });
  });

  [undefined, null, 'foo', Infinity, function() {}].forEach((bad) => {
    assert.throws(() => {
      validateRmOptionsSync(filePath, { force: bad });
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "options\.force" property must be of type boolean\./
    });
  });

  assert.throws(() => {
    validateRmOptionsSync(filePath, { retryDelay: -1 });
  }, {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: /^The value of "options\.retryDelay" is out of range\./
  });

  assert.throws(() => {
    validateRmOptionsSync(filePath, { maxRetries: -1 });
  }, {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: /^The value of "options\.maxRetries" is out of range\./
  });
}

{
  // IBMi has a different access permission mechanism
  // This test should not be run as `root`
  if (!common.isIBMi && (common.isWindows || process.getuid() !== 0)) {
    function makeDirectoryReadOnly(dir, allowExecute) {
      let accessErrorCode = 'EACCES';
      if (common.isMacOS && allowExecute) {
        accessErrorCode = 'ENOTEMPTY';
      }
      if (common.isWindows) {
        accessErrorCode = 'EPERM';
        const permissions = ['DE', 'DC'];
        if (!allowExecute) {
          permissions.push('X');
        }
        execSync(`icacls ${dir} /deny "everyone:(OI)(CI)(${permissions.join(',')})"`);
      } else {
        const mode = allowExecute ? 0o555 : 0o444;
        fs.chmodSync(dir, mode);
      }
      return accessErrorCode;
    }

    function makeDirectoryWritable(dir) {
      if (fs.existsSync(dir)) {
        if (common.isWindows) {
          execSync(`icacls ${dir} /remove:d "everyone"`);
        } else {
          fs.chmodSync(dir, 0o777);
        }
      }
    }

    {
      // Check that deleting a file that cannot be accessed using rmsync throws
      // https://github.com/nodejs/node/issues/38683
      const dirname = nextDirPath();
      const filePath = path.join(dirname, 'text.txt');
      try {
        fs.mkdirSync(dirname, common.mustNotMutateObjectDeep({ recursive: true }));
        fs.writeFileSync(filePath, 'hello');
        const code = makeDirectoryReadOnly(dirname, false);
        assert.throws(() => {
          fs.rmSync(filePath, common.mustNotMutateObjectDeep({ force: true }));
        }, {
          code,
          name: 'Error',
        });
      } finally {
        makeDirectoryWritable(dirname);
      }
    }

    {
      // Check endless recursion.
      // https://github.com/nodejs/node/issues/34580
      const dirname = nextDirPath();
      fs.mkdirSync(dirname, common.mustNotMutateObjectDeep({ recursive: true }));
      const root = fs.mkdtempSync(path.join(dirname, 'fs-'));
      const middle = path.join(root, 'middle');
      fs.mkdirSync(middle);
      fs.mkdirSync(path.join(middle, 'leaf')); // Make `middle` non-empty
      try {
        const code = makeDirectoryReadOnly(middle, true);
        try {
          assert.throws(() => {
            fs.rmSync(root, common.mustNotMutateObjectDeep({ recursive: true }));
          }, {
            code,
            name: 'Error',
          });
        } catch (err) {
          // Only fail the test if the folder was not deleted.
          // as in some cases rmSync successfully deletes read-only folders.
          if (fs.existsSync(root)) {
            throw err;
          }
        }
      } finally {
        makeDirectoryWritable(middle);
      }
    }
  }
}

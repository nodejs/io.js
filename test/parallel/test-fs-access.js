// Flags: --expose-internals
'use strict';

// This tests that fs.access and fs.accessSync works as expected
// and the errors thrown from these APIs include the desired properties

const common = require('../common');
if (!common.isWindows && process.getuid() === 0)
  common.skip('as this test should not be run as `root`');

if (common.isIBMi)
  common.skip('IBMi has a different access permission mechanism');

const assert = require('assert');
const fs = require('fs');

const { internalBinding } = require('internal/test/binding');
const { UV_ENOENT } = internalBinding('uv');

const tmpdir = require('../common/tmpdir');
const doesNotExist = tmpdir.resolve('__this_should_not_exist');
const readOnlyFile = tmpdir.resolve('read_only_file');
const readWriteFile = tmpdir.resolve('read_write_file');

function createFileWithPerms(file, mode) {
  fs.writeFileSync(file, '');
  fs.chmodSync(file, mode);
}

tmpdir.refresh();
createFileWithPerms(readOnlyFile, 0o444);
createFileWithPerms(readWriteFile, 0o666);

// On non-Windows supported platforms, fs.access(readOnlyFile, W_OK, ...)
// always succeeds if node runs as the super user, which is sometimes the
// case for tests running on our continuous testing platform agents.
//
// In this case, this test tries to change its process user id to a
// non-superuser user so that the test that checks for write access to a
// read-only file can be more meaningful.
//
// The change of user id is done after creating the fixtures files for the same
// reason: the test may be run as the superuser within a directory in which
// only the superuser can create files, and thus it may need superuser
// privileges to create them.
//
// There's not really any point in resetting the process' user id to 0 after
// changing it to 'nobody', since in the case that the test runs without
// superuser privilege, it is not possible to change its process user id to
// superuser.
//
// It can prevent the test from removing files created before the change of user
// id, but that's fine. In this case, it is the responsibility of the
// continuous integration platform to take care of that.
let hasWriteAccessForReadonlyFile = false;
if (!common.isWindows && process.getuid() === 0) {
  hasWriteAccessForReadonlyFile = true;
  try {
    process.setuid('nobody');
    hasWriteAccessForReadonlyFile = false;
  } catch {
    // Continue regardless of error.
  }
}

assert.strictEqual(typeof fs.constants.F_OK, 'number');
assert.strictEqual(typeof fs.constants.R_OK, 'number');
assert.strictEqual(typeof fs.constants.W_OK, 'number');
assert.strictEqual(typeof fs.constants.X_OK, 'number');

const throwNextTick = (e) => { process.nextTick(() => { throw e; }); };

fs.access(__filename, common.mustCall(function(...args) {
  assert.deepStrictEqual(args, [null]);
}));
fs.promises.access(__filename)
  .then(common.mustCall())
  .catch(throwNextTick);
fs.access(__filename, fs.constants.R_OK, common.mustCall(function(...args) {
  assert.deepStrictEqual(args, [null]);
}));
fs.promises.access(__filename, fs.constants.R_OK)
  .then(common.mustCall())
  .catch(throwNextTick);
fs.access(readOnlyFile, fs.constants.R_OK, common.mustCall(function(...args) {
  assert.deepStrictEqual(args, [null]);
}));
fs.promises.access(readOnlyFile, fs.constants.R_OK)
  .then(common.mustCall())
  .catch(throwNextTick);

{
  const expectedError = (err) => {
    assert.notStrictEqual(err, null);
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.path, doesNotExist);
  };
  const expectedErrorPromise = (err) => {
    expectedError(err);
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.path, doesNotExist);
  };
  fs.access(doesNotExist, common.mustCall(expectedError));
  fs.promises.access(doesNotExist)
    .then(common.mustNotCall(), common.mustCall(expectedErrorPromise))
    .catch(throwNextTick);
}

{
  function expectedError(err) {
    assert.strictEqual(this, undefined);
    if (hasWriteAccessForReadonlyFile) {
      assert.ifError(err);
    } else {
      assert.notStrictEqual(err, null);
      assert.strictEqual(err.path, readOnlyFile);
    }
  }
  fs.access(readOnlyFile, fs.constants.W_OK, common.mustCall(expectedError));
  fs.promises.access(readOnlyFile, fs.constants.W_OK)
    .then(common.mustNotCall(), common.mustCall(expectedError))
    .catch(throwNextTick);
}

{
  const expectedError = (err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_ARG_TYPE');
    assert.ok(err instanceof TypeError);
    return true;
  };
  assert.throws(
    () => { fs.access(100, fs.constants.F_OK, common.mustNotCall()); },
    expectedError
  );

  fs.promises.access(100, fs.constants.F_OK)
    .then(common.mustNotCall(), common.mustCall(expectedError))
    .catch(throwNextTick);
}

assert.throws(
  () => {
    fs.access(__filename, fs.constants.F_OK);
  },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

assert.throws(
  () => {
    fs.access(__filename, fs.constants.F_OK, common.mustNotMutateObjectDeep({}));
  },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

// Regular access should not throw.
fs.accessSync(__filename);
const mode = fs.constants.R_OK | fs.constants.W_OK;
fs.accessSync(readWriteFile, mode);

// Invalid modes should throw.
[
  false,
  1n,
  { [Symbol.toPrimitive]() { return fs.constants.R_OK; } },
  [1],
  'r',
].forEach((mode, i) => {
  console.log(mode, i);
  assert.throws(
    () => fs.access(readWriteFile, mode, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
    }
  );
  assert.throws(
    () => fs.accessSync(readWriteFile, mode),
    {
      code: 'ERR_INVALID_ARG_TYPE',
    }
  );
});

// Out of range modes should throw
[
  -1,
  8,
  Infinity,
  NaN,
].forEach((mode, i) => {
  console.log(mode, i);
  assert.throws(
    () => fs.access(readWriteFile, mode, common.mustNotCall()),
    {
      code: 'ERR_OUT_OF_RANGE',
    }
  );
  assert.throws(
    () => fs.accessSync(readWriteFile, mode),
    {
      code: 'ERR_OUT_OF_RANGE',
    }
  );
});

assert.throws(
  () => { fs.accessSync(doesNotExist); },
  (err) => {
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.path, doesNotExist);
    assert.strictEqual(
      err.message,
      `ENOENT: no such file or directory, access '${doesNotExist}'`
    );
    assert.strictEqual(err.constructor, Error);
    assert.strictEqual(err.syscall, 'access');
    assert.strictEqual(err.errno, UV_ENOENT);
    return true;
  }
);

assert.throws(
  () => { fs.accessSync(Buffer.from(doesNotExist)); },
  (err) => {
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.path, doesNotExist);
    assert.strictEqual(
      err.message,
      `ENOENT: no such file or directory, access '${doesNotExist}'`
    );
    assert.strictEqual(err.constructor, Error);
    assert.strictEqual(err.syscall, 'access');
    assert.strictEqual(err.errno, UV_ENOENT);
    return true;
  }
);

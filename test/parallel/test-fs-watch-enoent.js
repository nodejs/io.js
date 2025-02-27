// Flags: --expose-internals
'use strict';

// This verifies the error thrown by fs.watch.

const common = require('../common');

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const tmpdir = require('../common/tmpdir');
const nonexistentFile = tmpdir.resolve('non-existent');
const { internalBinding } = require('internal/test/binding');
const {
  UV_ENODEV,
  UV_ENOENT
} = internalBinding('uv');

tmpdir.refresh();

{
  const validateError = (err) => {
    assert.strictEqual(err.path, path.toNamespacedPath(nonexistentFile));
    assert.strictEqual(err.filename, path.toNamespacedPath(nonexistentFile));
    assert.ok(err.syscall === 'watch' || err.syscall === 'stat');
    if (err.code === 'ENOENT') {
      assert.strictEqual(err.errno, UV_ENOENT);
      assert.strictEqual(err.code, 'ENOENT');
    } else {  // AIX
      assert.strictEqual(err.errno, UV_ENODEV);
      assert.strictEqual(err.code, 'ENODEV');
    }
    return true;
  };

  assert.throws(
    () => fs.watch(nonexistentFile, common.mustNotCall()),
    validateError
  );
}

{
  if (common.isMacOS || common.isWindows) {
    const file = tmpdir.resolve('file-to-watch');
    fs.writeFileSync(file, 'test');
    const watcher = fs.watch(file, common.mustNotCall());

    const validateError = (err) => {
      assert.strictEqual(err.path, path.toNamespacedPath(nonexistentFile));
      assert.strictEqual(err.filename, path.toNamespacedPath(nonexistentFile));
      assert.strictEqual(err.errno, UV_ENOENT);
      assert.strictEqual(err.code, 'ENOENT');
      assert.strictEqual(err.syscall, 'watch');
      fs.unlinkSync(file);
      return true;
    };

    watcher.on('error', common.mustCall(validateError));

    // Simulate the invocation from the binding
    watcher._handle.onchange(UV_ENOENT, 'ENOENT', nonexistentFile);
  }
}

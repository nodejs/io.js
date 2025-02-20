'use strict';
require('../common');
const tmpdir = require('../common/tmpdir');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

// This test ensures that fs.rmSync handles non-ASCII characters in file paths,
// and that errors contain correctly encoded paths and err.path values.

tmpdir.refresh(); // Prepare a clean temporary directory

// Define paths with non-ASCII characters
const dirPath = path.join(tmpdir.path, '速_dir');
const filePath = path.join(tmpdir.path, '速.txt');

// Create a directory and a file with non-ASCII characters
fs.mkdirSync(dirPath);
fs.writeFileSync(filePath, 'This is a test file with special characters.');
fs.rmSync(filePath);
assert.strictEqual(fs.existsSync(filePath), false);

// Ensure rmSync throws an error when trying to remove a directory without recursive
assert.throws(() => {
  fs.rmSync(dirPath, { recursive: false });
}, (err) => {
  // Assert the error code and check that the error message includes the correct non-ASCII path
  assert.strictEqual(err.code, 'ERR_FS_EISDIR');
  assert(err.message.includes(dirPath), 'Error message should include the directory path');
  assert.strictEqual(err.path, dirPath);
  return true;
});

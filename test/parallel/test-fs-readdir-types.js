// Flags: --expose-internals
'use strict';

const common = require('../common');
const assert = require('assert');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');

const { internalBinding } = require('internal/test/binding');
const binding = internalBinding('fs');

const readdirDir = tmpdir.path;
const files = ['empty', 'files', 'for', 'just', 'testing'];
const constants = require('fs').constants;
const types = {
  isDirectory: constants.UV_DIRENT_DIR,
  isFile: constants.UV_DIRENT_FILE,
  isBlockDevice: constants.UV_DIRENT_BLOCK,
  isCharacterDevice: constants.UV_DIRENT_CHAR,
  isSymbolicLink: constants.UV_DIRENT_LINK,
  isFIFO: constants.UV_DIRENT_FIFO,
  isSocket: constants.UV_DIRENT_SOCKET
};
const typeMethods = Object.keys(types);

// Make sure tmp directory is clean
tmpdir.refresh();

// Create the necessary files
files.forEach(function(currentFile) {
  fs.writeFileSync(`${readdirDir}/${currentFile}`, '', 'utf8');
});


function assertDirents(dirents) {
  assert.strictEqual(files.length, dirents.length);
  for (const [i, dirent] of dirents.entries()) {
    assert(dirent instanceof fs.Dirent);
    assert.strictEqual(dirent.name, files[i]);
    assert.strictEqual(dirent.isFile(), true);
    assert.strictEqual(dirent.isDirectory(), false);
    assert.strictEqual(dirent.isSocket(), false);
    assert.strictEqual(dirent.isBlockDevice(), false);
    assert.strictEqual(dirent.isCharacterDevice(), false);
    assert.strictEqual(dirent.isFIFO(), false);
    assert.strictEqual(dirent.isSymbolicLink(), false);
  }
}

// Check the readdir Sync version
assertDirents(fs.readdirSync(readdirDir, { withFileTypes: true }));

fs.readdir(__filename, {
  withFileTypes: true
}, common.mustCall((err) => {
  assert.throws(
    () => { throw err; },
    {
      code: 'ENOTDIR',
      name: 'Error',
      message: `ENOTDIR: not a directory, scandir '${__filename}'`
    }
  );
}));

// Check the readdir async version
fs.readdir(readdirDir, {
  withFileTypes: true
}, common.mustSucceed((dirents) => {
  assertDirents(dirents);
}));

(async () => {
  const dirents = await fs.promises.readdir(readdirDir, {
    withFileTypes: true
  });
  assertDirents(dirents);
})().then(common.mustCall());

// Check that mutating options doesn't affect results
(async () => {
  const options = { withFileTypes: true };
  const direntsPromise = fs.promises.readdir(readdirDir, options);
  options.withFileTypes = false;
  assertDirents(await direntsPromise);
})().then(common.mustCall());

{
  const options = { recursive: true, withFileTypes: true };
  fs.readdir(readdirDir, options, common.mustSucceed((dirents) => {
    assertDirents(dirents);
  }));
  options.withFileTypes = false;
}

// Check for correct types when the binding returns unknowns
const UNKNOWN = constants.UV_DIRENT_UNKNOWN;
const oldReaddir = binding.readdir;
process.on('beforeExit', () => { binding.readdir = oldReaddir; });
binding.readdir = common.mustCall((path, encoding, types, req, ctx) => {
  if (req) {
    const oldCb = req.oncomplete;
    req.oncomplete = (err, results) => {
      if (err) {
        oldCb(err);
        return;
      }
      results[1] = results[1].map(() => UNKNOWN);
      oldCb(null, results);
    };
    oldReaddir(path, encoding, types, req);
  } else {
    const results = oldReaddir(path, encoding, types);
    results[1] = results[1].map(() => UNKNOWN);
    return results;
  }
}, 2);
assertDirents(fs.readdirSync(readdirDir, { withFileTypes: true }));
fs.readdir(readdirDir, {
  withFileTypes: true
}, common.mustSucceed((dirents) => {
  assertDirents(dirents);
}));

// Dirent types
for (const method of typeMethods) {
  const dirent = new fs.Dirent('foo', types[method]);
  for (const testMethod of typeMethods) {
    assert.strictEqual(dirent[testMethod](), testMethod === method);
  }
}

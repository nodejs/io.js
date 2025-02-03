'use strict';

const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const fsPromises = fs.promises;
const pathModule = require('path');
const tmpdir = require('../common/tmpdir');

const testDir = tmpdir.path;

const fileStructure = [
  [ 'a', [ 'a', 'foo', 'bar' ] ],
  [ 'b', [ 'foo', 'bar' ] ],
  [ 'c', [ 'foo', 'bar' ] ],
  [ 'd', [ 'foo', 'bar' ] ],
  [ 'e', [ 'foo', 'bar' ] ],
  [ 'f', [ 'foo', 'bar' ] ],
  [ 'g', [ 'foo', 'bar' ] ],
  [ 'h', [ 'foo', 'bar' ] ],
  [ 'i', [ 'foo', 'bar' ] ],
  [ 'j', [ 'foo', 'bar' ] ],
  [ 'k', [ 'foo', 'bar' ] ],
  [ 'l', [ 'foo', 'bar' ] ],
  [ 'm', [ 'foo', 'bar' ] ],
  [ 'n', [ 'foo', 'bar' ] ],
  [ 'o', [ 'foo', 'bar' ] ],
  [ 'p', [ 'foo', 'bar' ] ],
  [ 'q', [ 'foo', 'bar' ] ],
  [ 'r', [ 'foo', 'bar' ] ],
  [ 's', [ 'foo', 'bar' ] ],
  [ 't', [ 'foo', 'bar' ] ],
  [ 'u', [ 'foo', 'bar' ] ],
  [ 'v', [ 'foo', 'bar' ] ],
  [ 'w', [ 'foo', 'bar' ] ],
  [ 'x', [ 'foo', 'bar' ] ],
  [ 'y', [ 'foo', 'bar' ] ],
  [ 'z', [ 'foo', 'bar' ] ],
  [ 'aa', [ 'foo', 'bar' ] ],
  [ 'bb', [ 'foo', 'bar' ] ],
  [ 'cc', [ 'foo', 'bar' ] ],
  [ 'dd', [ 'foo', 'bar' ] ],
  [ 'ee', [ 'foo', 'bar' ] ],
  [ 'ff', [ 'foo', 'bar' ] ],
  [ 'gg', [ 'foo', 'bar' ] ],
  [ 'hh', [ 'foo', 'bar' ] ],
  [ 'ii', [ 'foo', 'bar' ] ],
  [ 'jj', [ 'foo', 'bar' ] ],
  [ 'kk', [ 'foo', 'bar' ] ],
  [ 'll', [ 'foo', 'bar' ] ],
  [ 'mm', [ 'foo', 'bar' ] ],
  [ 'nn', [ 'foo', 'bar' ] ],
  [ 'oo', [ 'foo', 'bar' ] ],
  [ 'pp', [ 'foo', 'bar' ] ],
  [ 'qq', [ 'foo', 'bar' ] ],
  [ 'rr', [ 'foo', 'bar' ] ],
  [ 'ss', [ 'foo', 'bar' ] ],
  [ 'tt', [ 'foo', 'bar' ] ],
  [ 'uu', [ 'foo', 'bar' ] ],
  [ 'vv', [ 'foo', 'bar' ] ],
  [ 'ww', [ 'foo', 'bar' ] ],
  [ 'xx', [ 'foo', 'bar' ] ],
  [ 'yy', [ 'foo', 'bar' ] ],
  [ 'zz', [ 'foo', 'bar' ] ],
  [ 'abc', [ ['def', [ 'foo', 'bar' ] ], ['ghi', [ 'foo', 'bar' ] ] ] ],
];

function createFiles(path, fileStructure) {
  for (const fileOrDir of fileStructure) {
    if (typeof fileOrDir === 'string') {
      fs.writeFileSync(pathModule.join(path, fileOrDir), '');
    } else {
      const dirPath = pathModule.join(path, fileOrDir[0]);
      fs.mkdirSync(dirPath);
      createFiles(dirPath, fileOrDir[1]);
    }
  }
}

// Make sure tmp directory is clean
tmpdir.refresh();

createFiles(testDir, fileStructure);
const symlinksRootPath = pathModule.join(testDir, 'symlinks');
const symlinkTargetFile = pathModule.join(symlinksRootPath, 'symlink-target-file');
const symlinkTargetDir = pathModule.join(symlinksRootPath, 'symlink-target-dir');
fs.mkdirSync(symlinksRootPath);
fs.writeFileSync(symlinkTargetFile, '');
fs.mkdirSync(symlinkTargetDir);
fs.symlinkSync(symlinkTargetFile, pathModule.join(symlinksRootPath, 'symlink-src-file'));
fs.symlinkSync(symlinkTargetDir, pathModule.join(symlinksRootPath, 'symlink-src-dir'));

const expected = [
  'a', 'a/a', 'a/bar', 'a/foo', 'aa', 'aa/bar', 'aa/foo',
  'abc', 'abc/def', 'abc/def/bar', 'abc/def/foo', 'abc/ghi', 'abc/ghi/bar', 'abc/ghi/foo',
  'b', 'b/bar', 'b/foo', 'bb', 'bb/bar', 'bb/foo',
  'c', 'c/bar', 'c/foo', 'cc', 'cc/bar', 'cc/foo',
  'd', 'd/bar', 'd/foo', 'dd', 'dd/bar', 'dd/foo',
  'e', 'e/bar', 'e/foo', 'ee', 'ee/bar', 'ee/foo',
  'f', 'f/bar', 'f/foo', 'ff', 'ff/bar', 'ff/foo',
  'g', 'g/bar', 'g/foo', 'gg', 'gg/bar', 'gg/foo',
  'h', 'h/bar', 'h/foo', 'hh', 'hh/bar', 'hh/foo',
  'i', 'i/bar', 'i/foo', 'ii', 'ii/bar', 'ii/foo',
  'j', 'j/bar', 'j/foo', 'jj', 'jj/bar', 'jj/foo',
  'k', 'k/bar', 'k/foo', 'kk', 'kk/bar', 'kk/foo',
  'l', 'l/bar', 'l/foo', 'll', 'll/bar', 'll/foo',
  'm', 'm/bar', 'm/foo', 'mm', 'mm/bar', 'mm/foo',
  'n', 'n/bar', 'n/foo', 'nn', 'nn/bar', 'nn/foo',
  'o', 'o/bar', 'o/foo', 'oo', 'oo/bar', 'oo/foo',
  'p', 'p/bar', 'p/foo', 'pp', 'pp/bar', 'pp/foo',
  'q', 'q/bar', 'q/foo', 'qq', 'qq/bar', 'qq/foo',
  'r', 'r/bar', 'r/foo', 'rr', 'rr/bar', 'rr/foo',
  's', 's/bar', 's/foo', 'ss', 'ss/bar', 'ss/foo',
  'symlinks', 'symlinks/symlink-src-dir', 'symlinks/symlink-src-file',
  'symlinks/symlink-target-dir', 'symlinks/symlink-target-file',
  't', 't/bar', 't/foo', 'tt', 'tt/bar', 'tt/foo',
  'u', 'u/bar', 'u/foo', 'uu', 'uu/bar', 'uu/foo',
  'v', 'v/bar', 'v/foo', 'vv', 'vv/bar', 'vv/foo',
  'w', 'w/bar', 'w/foo', 'ww', 'ww/bar', 'ww/foo',
  'x', 'x/bar', 'x/foo', 'xx', 'xx/bar', 'xx/foo',
  'y', 'y/bar', 'y/foo', 'yy', 'yy/bar', 'yy/foo',
  'z', 'z/bar', 'z/foo', 'zz', 'zz/bar', 'zz/foo',
];

// Normalize paths once for non POSIX platforms
for (let i = 0; i < expected.length; i++) {
  expected[i] = pathModule.normalize(expected[i]);
}

function getDirentPath(dirent) {
  return pathModule.relative(testDir, pathModule.join(dirent.parentPath, dirent.name));
}

function assertDirents(dirents) {
  assert.strictEqual(dirents.length, expected.length);
  dirents.sort((a, b) => (getDirentPath(a) < getDirentPath(b) ? -1 : 1));
  assert.deepStrictEqual(
    dirents.map((dirent) => {
      assert(dirent instanceof fs.Dirent);
      return getDirentPath(dirent);
    }),
    expected
  );
}

function processDirSync(dir) {
  const dirents = [];
  let dirent = dir.readSync();
  while (dirent !== null) {
    dirents.push(dirent);
    dirent = dir.readSync();
  }
  assertDirents(dirents);
}

// Opendir read results sync

{
  const dir = fs.opendirSync(testDir, { recursive: true });
  processDirSync(dir);
  dir.closeSync();
}

{
  fs.opendir(testDir, { recursive: true }, common.mustSucceed((dir) => {
    processDirSync(dir);
    dir.close(common.mustSucceed());
  }));
}

// Opendir read result using callback

function processDirCb(dir, cb) {
  const acc = [];

  function _process(dir, acc, cb) {
    dir.read((err, dirent) => {
      if (err) {
        return cb(err);
      }

      if (dirent !== null) {
        acc.push(dirent);
        _process(dir, acc, cb);
      } else {
        cb(null, acc);
      }
    });
  }

  _process(dir, acc, cb);
}

{
  const dir = fs.opendirSync(testDir, { recursive: true });
  processDirCb(dir, common.mustSucceed((dirents) => {
    assertDirents(dirents);
    dir.close(common.mustSucceed());
  }));
}

{
  fs.opendir(testDir, { recursive: true }, common.mustSucceed((dir) => {
    processDirCb(dir, common.mustSucceed((dirents) => {
      assertDirents(dirents);
      dir.close(common.mustSucceed());
    }));
  }));
}

// Opendir read result using AsyncIterator

{
  async function test() {
    const dir = await fsPromises.opendir(testDir, { recursive: true });
    const dirents = [];
    for await (const dirent of dir) {
      dirents.push(dirent);
    }
    assertDirents(dirents);
  }

  test().then(common.mustCall());
}

// Issue https://github.com/nodejs/node/issues/48820 highlights that
// opendir recursive does not properly handle the buffer size option.
// This test asserts that the buffer size option is respected.
{
  const dir = fs.opendirSync(testDir, { bufferSize: 1, recursive: true });
  processDirSync(dir);
  dir.closeSync();
}

{
  fs.opendir(testDir, { recursive: true, bufferSize: 1 }, common.mustSucceed((dir) => {
    processDirCb(dir, common.mustSucceed((dirents) => {
      assertDirents(dirents);
      dir.close(common.mustSucceed());
    }));
  }));
}

'use strict';
const common = require('../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const tmpdir = require('../common/tmpdir');

// Basic usage tests.
assert.throws(
  () => {
    fs.watchFile('./some-file');
  },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

assert.throws(
  () => {
    fs.watchFile('./another-file', {}, 'bad listener');
  },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

assert.throws(() => {
  fs.watchFile(new Object(), common.mustNotCall());
}, { code: 'ERR_INVALID_ARG_TYPE', name: 'TypeError' });

const enoentFile = tmpdir.resolve('non-existent-file');
const expectedStatObject = new fs.Stats(
  0,                                        // dev
  0,                                        // mode
  0,                                        // nlink
  0,                                        // uid
  0,                                        // gid
  0,                                        // rdev
  0,                                        // blksize
  0,                                        // ino
  0,                                        // size
  0,                                        // blocks
  Date.UTC(1970, 0, 1, 0, 0, 0),            // atime
  Date.UTC(1970, 0, 1, 0, 0, 0),            // mtime
  Date.UTC(1970, 0, 1, 0, 0, 0),            // ctime
  Date.UTC(1970, 0, 1, 0, 0, 0)             // birthtime
);

tmpdir.refresh();

// If the file initially didn't exist, and gets created at a later point of
// time, the callback should be invoked again with proper values in stat object
let fileExists = false;

const watcher =
  fs.watchFile(enoentFile, { interval: 0 }, common.mustCall((curr, prev) => {
    if (!fileExists) {
      // If the file does not exist, all the fields should be zero and the date
      // fields should be UNIX EPOCH time
      assert.deepStrictEqual(curr, expectedStatObject);
      assert.deepStrictEqual(prev, expectedStatObject);
      // Create the file now, so that the callback will be called back once the
      // event loop notices it.
      fs.closeSync(fs.openSync(enoentFile, 'w'));
      fileExists = true;
    } else {
      // If the ino (inode) value is greater than zero, it means that the file
      // is present in the filesystem and it has a valid inode number.
      assert(curr.ino > 0);
      // As the file just got created, previous ino value should be lesser than
      // or equal to zero (non-existent file).
      assert(prev.ino <= 0);
      // Stop watching the file
      fs.unwatchFile(enoentFile);
      watcher.stop();  // Stopping a stopped watcher should be a noop
    }
  }, 2));

// 'stop' should only be emitted once - stopping a stopped watcher should
// not trigger a 'stop' event.
watcher.on('stop', common.mustCall());

// Watch events should callback with a filename on supported systems.
// Omitting AIX. It works but not reliably.
if (common.isLinux || common.isMacOS || common.isWindows) {
  const dir = tmpdir.resolve('watch');
  function doWatch() {
    const handle = fs.watch(dir, common.mustCall(function(eventType, filename) {
      clearInterval(interval);
      handle.close();
      assert.strictEqual(filename, 'foo.txt');
    }));

    const interval = setInterval(() => {
      fs.writeFile(path.join(dir, 'foo.txt'), 'foo', common.mustCall((err) => {
        if (err) assert.fail(err);
      }));
    }, 1);
  }

  fs.mkdir(dir, common.mustSucceed(() => {
    if (common.isMacOS) {
      // On macOS delay watcher start to avoid leaking previous events.
      // Refs: https://github.com/libuv/libuv/pull/4503
      setTimeout(doWatch, common.platformTimeout(100));
    } else {
      doWatch();
    }
  }));
}

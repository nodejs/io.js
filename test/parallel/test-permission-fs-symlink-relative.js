// Flags: --experimental-permission --allow-fs-read=* --allow-fs-write=*
'use strict';

const common = require('../common');
common.skipIfWorker();

const assert = require('assert');
const path = require('path');
const { symlinkSync, symlink, promises: { symlink: symlinkAsync } } = require('fs');

const error = {
  code: 'ERR_ACCESS_DENIED',
  message: /relative symbolic link target/,
};

for (const targetString of ['a', './b/c', '../d', 'e/../f', 'C:drive-relative', 'ntfs:alternate']) {
  for (const target of [targetString, Buffer.from(targetString)]) {
    for (const path of [__filename, __dirname, process.execPath]) {
      assert.throws(() => symlinkSync(target, path), error);
      symlink(target, path, common.mustCall((err) => {
        assert(err);
        assert.strictEqual(err.code, error.code);
        assert.match(err.message, error.message);
      }));
      assert.rejects(() => symlinkAsync(target, path), error).then(common.mustCall());
    }
  }
}

// Absolute should not throw
for (const targetString of [path.resolve('.')]) {
  for (const target of [targetString, Buffer.from(targetString)]) {
    for (const path of [__filename]) {
      symlink(target, path, common.mustCall((err) => {
        assert(err);
        assert.strictEqual(err.code, 'EEXIST');
        assert.match(err.message, /file already exists/);
      }));
    }
  }
}

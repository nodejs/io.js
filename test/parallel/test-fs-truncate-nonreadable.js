'use strict';

const common = require('../common');

// This test ensures that truncate works on non-readable files

const assert = require('assert');
const fs = require('fs');
const fsPromises = fs.promises;
const path = require('path');
const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const expected = Buffer.from('good');

function checkContents(filepath) {
  fs.chmodSync(filepath, 0o400);
  assert.deepStrictEqual(
    fs.readFileSync(filepath),
    expected
  );
}

(async () => {
  const MODE = 0o200;
  {
    const filepath = path.resolve(tmpdir.path, 'promises.txt');
    fs.writeFileSync(filepath, 'good bad');
    fs.chmodSync(filepath, MODE);
    await fsPromises.truncate(filepath, 4);
    checkContents(filepath);
  }
  {
    const filepath = path.resolve(tmpdir.path, 'callback.txt');
    fs.writeFileSync(filepath, 'good bad');
    fs.chmodSync(filepath, MODE);
    fs.truncate(filepath, 4, common.mustSucceed(() => {
      checkContents(filepath);
    }));
  }
  {
    const filepath = path.resolve(tmpdir.path, 'synchronous.txt');
    fs.writeFileSync(filepath, 'good bad');
    fs.chmodSync(filepath, MODE);
    fs.truncateSync(filepath, 4);
    checkContents(filepath);
  }
})().then(common.mustCall());

// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');

const currentFileData = 'ABCD';
const fixtures = require('../common/fixtures');
const s = fixtures.utf8TestText;

tmpdir.refresh();

const throwNextTick = (e) => { process.nextTick(() => { throw e; }); };

// Test that empty file will be created and have content added (callback API).
{
  const filename = tmpdir.resolve('append.txt');

  fs.appendFile(filename, s, common.mustSucceed(() => {
    fs.readFile(filename, common.mustSucceed((buffer) => {
      assert.strictEqual(Buffer.byteLength(s), buffer.length);
    }));
  }));
}

// Test that empty file will be created and have content added (promise API).
{
  const filename = tmpdir.resolve('append-promise.txt');

  fs.promises.appendFile(filename, s)
    .then(common.mustCall(() => fs.promises.readFile(filename)))
    .then((buffer) => {
      assert.strictEqual(Buffer.byteLength(s), buffer.length);
    })
    .catch(throwNextTick);
}

// Test that appends data to a non-empty file (callback API).
{
  const filename = tmpdir.resolve('append-non-empty.txt');
  fs.writeFileSync(filename, currentFileData);

  fs.appendFile(filename, s, common.mustSucceed(() => {
    fs.readFile(filename, common.mustSucceed((buffer) => {
      assert.strictEqual(Buffer.byteLength(s) + currentFileData.length,
                         buffer.length);
    }));
  }));
}

// Test that appends data to a non-empty file (promise API).
{
  const filename = tmpdir.resolve('append-non-empty-promise.txt');
  fs.writeFileSync(filename, currentFileData);

  fs.promises.appendFile(filename, s)
    .then(common.mustCall(() => fs.promises.readFile(filename)))
    .then((buffer) => {
      assert.strictEqual(Buffer.byteLength(s) + currentFileData.length,
                         buffer.length);
    })
    .catch(throwNextTick);
}

// Test that appendFile accepts buffers (callback API).
{
  const filename = tmpdir.resolve('append-buffer.txt');
  fs.writeFileSync(filename, currentFileData);

  const buf = Buffer.from(s, 'utf8');

  fs.appendFile(filename, buf, common.mustSucceed(() => {
    fs.readFile(filename, common.mustSucceed((buffer) => {
      assert.strictEqual(buf.length + currentFileData.length, buffer.length);
    }));
  }));
}

// Test that appendFile accepts buffers (promises API).
{
  const filename = tmpdir.resolve('append-buffer-promises.txt');
  fs.writeFileSync(filename, currentFileData);

  const buf = Buffer.from(s, 'utf8');

  fs.promises.appendFile(filename, buf)
    .then(common.mustCall(() => fs.promises.readFile(filename)))
    .then((buffer) => {
      assert.strictEqual(buf.length + currentFileData.length, buffer.length);
    })
    .catch(throwNextTick);
}

// Test that appendFile does not accept invalid data type (callback API).
[false, 5, {}, null, undefined].forEach(async (data) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /"data"|"buffer"/
  };
  const filename = tmpdir.resolve('append-invalid-data.txt');

  assert.throws(
    () => fs.appendFile(filename, data, common.mustNotCall()),
    errObj
  );

  assert.throws(
    () => fs.appendFileSync(filename, data),
    errObj
  );

  await assert.rejects(
    fs.promises.appendFile(filename, data),
    errObj
  );
  // The filename shouldn't exist if throwing error.
  assert.throws(
    () => fs.statSync(filename),
    {
      code: 'ENOENT',
      message: /no such file or directory/
    }
  );
});

// Test that appendFile accepts file descriptors (callback API).
{
  const filename = tmpdir.resolve('append-descriptors.txt');
  fs.writeFileSync(filename, currentFileData);

  fs.open(filename, 'a+', common.mustSucceed((fd) => {
    fs.appendFile(fd, s, common.mustSucceed(() => {
      fs.close(fd, common.mustSucceed(() => {
        fs.readFile(filename, common.mustSucceed((buffer) => {
          assert.strictEqual(Buffer.byteLength(s) + currentFileData.length,
                             buffer.length);
        }));
      }));
    }));
  }));
}

// Test that appendFile accepts file descriptors (promises API).
{
  const filename = tmpdir.resolve('append-descriptors-promises.txt');
  fs.writeFileSync(filename, currentFileData);

  let fd;
  fs.promises.open(filename, 'a+')
    .then(common.mustCall((fileDescriptor) => {
      fd = fileDescriptor;
      return fs.promises.appendFile(fd, s);
    }))
    .then(common.mustCall(() => fd.close()))
    .then(common.mustCall(() => fs.promises.readFile(filename)))
    .then(common.mustCall((buffer) => {
      assert.strictEqual(Buffer.byteLength(s) + currentFileData.length,
                         buffer.length);
    }))
    .catch(throwNextTick);
}

assert.throws(
  () => fs.appendFile(tmpdir.resolve('append6.txt'), console.log),
  { code: 'ERR_INVALID_ARG_TYPE' });

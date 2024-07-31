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
const path = require('path');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');
const tmp = tmpdir.path;
const filename = path.resolve(tmp, 'truncate-file.txt');
const data = Buffer.alloc(1024 * 16, 'x');

tmpdir.refresh();

let stat;

const msg = 'Using fs.truncate with a file descriptor is deprecated.' +
            ' Please use fs.ftruncate with a file descriptor instead.';

// Check truncateSync
fs.writeFileSync(filename, data);
stat = fs.statSync(filename);
assert.strictEqual(stat.size, 1024 * 16);

fs.truncateSync(filename, 1024);
stat = fs.statSync(filename);
assert.strictEqual(stat.size, 1024);

fs.truncateSync(filename);
stat = fs.statSync(filename);
assert.strictEqual(stat.size, 0);

// Check ftruncateSync
fs.writeFileSync(filename, data);
const fd = fs.openSync(filename, 'r+');

stat = fs.statSync(filename);
assert.strictEqual(stat.size, 1024 * 16);

fs.ftruncateSync(fd, 1024);
stat = fs.statSync(filename);
assert.strictEqual(stat.size, 1024);

fs.ftruncateSync(fd);
stat = fs.statSync(filename);
assert.strictEqual(stat.size, 0);

// truncateSync
common.expectWarning('DeprecationWarning', msg, 'DEP0081');
fs.truncateSync(fd);

fs.closeSync(fd);

// Async tests
testTruncate(common.mustSucceed(() => {
  testFtruncate(common.mustSucceed());
}));

function testTruncate(cb) {
  fs.writeFile(filename, data, function(er) {
    if (er) return cb(er);
    fs.stat(filename, function(er, stat) {
      if (er) return cb(er);
      assert.strictEqual(stat.size, 1024 * 16);

      fs.truncate(filename, 1024, function(er) {
        if (er) return cb(er);
        fs.stat(filename, function(er, stat) {
          if (er) return cb(er);
          assert.strictEqual(stat.size, 1024);

          fs.truncate(filename, function(er) {
            if (er) return cb(er);
            fs.stat(filename, function(er, stat) {
              if (er) return cb(er);
              assert.strictEqual(stat.size, 0);
              cb();
            });
          });
        });
      });
    });
  });
}

function testFtruncate(cb) {
  fs.writeFile(filename, data, function(er) {
    if (er) return cb(er);
    fs.stat(filename, function(er, stat) {
      if (er) return cb(er);
      assert.strictEqual(stat.size, 1024 * 16);

      fs.open(filename, 'w', function(er, fd) {
        if (er) return cb(er);
        fs.ftruncate(fd, 1024, function(er) {
          if (er) return cb(er);
          fs.stat(filename, function(er, stat) {
            if (er) return cb(er);
            assert.strictEqual(stat.size, 1024);

            fs.ftruncate(fd, function(er) {
              if (er) return cb(er);
              fs.stat(filename, function(er, stat) {
                if (er) return cb(er);
                assert.strictEqual(stat.size, 0);
                fs.close(fd, cb);
              });
            });
          });
        });
      });
    });
  });
}

// Make sure if the size of the file is smaller than the length then it is
// filled with zeroes.

{
  const file1 = path.resolve(tmp, 'truncate-file-1.txt');
  fs.writeFileSync(file1, 'Hi');
  fs.truncateSync(file1, 4);
  assert(fs.readFileSync(file1).equals(Buffer.from('Hi\u0000\u0000')));
}

{
  const file2 = path.resolve(tmp, 'truncate-file-2.txt');
  fs.writeFileSync(file2, 'Hi');
  const fd = fs.openSync(file2, 'r+');
  process.on('beforeExit', () => fs.closeSync(fd));
  fs.ftruncateSync(fd, 4);
  assert(fs.readFileSync(file2).equals(Buffer.from('Hi\u0000\u0000')));
}

{
  const file3 = path.resolve(tmp, 'truncate-file-3.txt');
  fs.writeFileSync(file3, 'Hi');
  fs.truncate(file3, 4, common.mustSucceed(() => {
    assert(fs.readFileSync(file3).equals(Buffer.from('Hi\u0000\u0000')));
  }));
}

{
  const file4 = path.resolve(tmp, 'truncate-file-4.txt');
  fs.writeFileSync(file4, 'Hi');
  const fd = fs.openSync(file4, 'r+');
  process.on('beforeExit', () => fs.closeSync(fd));
  fs.ftruncate(fd, 4, common.mustSucceed(() => {
    assert(fs.readFileSync(file4).equals(Buffer.from('Hi\u0000\u0000')));
  }));
}

{
  const file5 = path.resolve(tmp, 'truncate-file-5.txt');
  fs.writeFileSync(file5, 'Hi');
  const fd = fs.openSync(file5, 'r+');
  process.on('beforeExit', () => fs.closeSync(fd));

  ['', false, null, {}, []].forEach((input) => {
    const received = common.invalidArgTypeHelper(input);
    assert.throws(
      () => fs.truncate(file5, input, common.mustNotCall()),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: `The "len" argument must be of type number.${received}`
      }
    );

    assert.throws(
      () => fs.ftruncate(fd, input),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: `The "len" argument must be of type number.${received}`
      }
    );
  });

  [-1.5, 1.5].forEach((input) => {
    assert.throws(
      () => fs.truncate(file5, input),
      {
        code: 'ERR_OUT_OF_RANGE',
        name: 'RangeError',
        message: 'The value of "len" is out of range. It must be ' +
                  `an integer. Received ${input}`
      }
    );

    assert.throws(
      () => fs.ftruncate(fd, input),
      {
        code: 'ERR_OUT_OF_RANGE',
        name: 'RangeError',
        message: 'The value of "len" is out of range. It must be ' +
                  `an integer. Received ${input}`
      }
    );
  });

  fs.ftruncate(fd, undefined, common.mustSucceed(() => {
    assert(fs.readFileSync(file5).equals(Buffer.from('')));
  }));
}

{
  const file6 = path.resolve(tmp, 'truncate-file-6.txt');
  fs.writeFileSync(file6, 'Hi');
  const fd = fs.openSync(file6, 'r+');
  process.on('beforeExit', () => fs.closeSync(fd));
  fs.ftruncate(fd, -1, common.mustSucceed(() => {
    assert(fs.readFileSync(file6).equals(Buffer.from('')));
  }));
}

{
  const file7 = path.resolve(tmp, 'truncate-file-7.txt');
  fs.writeFileSync(file7, 'Hi');
  fs.truncate(file7, undefined, common.mustSucceed(() => {
    assert(fs.readFileSync(file7).equals(Buffer.from('')));
  }));
}

{
  const file8 = path.resolve(tmp, 'non-existent-truncate-file.txt');
  const validateError = (err) => {
    assert.strictEqual(file8, err.path);
    assert.strictEqual(
      err.message,
      `ENOENT: no such file or directory, open '${file8}'`);
    assert.strictEqual(err.code, 'ENOENT');
    assert.strictEqual(err.syscall, 'open');
    return true;
  };
  fs.truncate(file8, 0, common.mustCall(validateError));
}

['', false, null, {}, []].forEach((input) => {
  assert.throws(
    () => fs.truncate('/foo/bar', input),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "len" argument must be of type number.' +
               common.invalidArgTypeHelper(input)
    }
  );
});

['', false, null, undefined, {}, []].forEach((input) => {
  ['ftruncate', 'ftruncateSync'].forEach((fnName) => {
    assert.throws(
      () => fs[fnName](input, 1, () => {}),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "fd" argument must be of type number.' +
                 common.invalidArgTypeHelper(input)
      }
    );
  });
});

{
  const file1 = path.resolve(tmp, 'truncate-file-1.txt');
  fs.writeFileSync(file1, 'Hi');
  fs.truncateSync(file1, -1);  // Negative coerced to 0, No error.
  assert(fs.readFileSync(file1).equals(Buffer.alloc(0)));
}

{
  const file1 = path.resolve(tmp, 'truncate-file-2.txt');
  fs.writeFileSync(file1, 'Hi');
  // Negative coerced to 0, No error.
  fs.truncate(file1, -1, common.mustSucceed(() => {
    assert(fs.readFileSync(file1).equals(Buffer.alloc(0)));
  }));
}

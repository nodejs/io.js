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
const tmpdir = require('../common/tmpdir');

const child_process = require('child_process');
const assert = require('assert');
const fs = require('fs');
const fixtures = require('../common/fixtures');

const fn = fixtures.path('elipses.txt');
const rangeFile = fixtures.path('x.txt');

function test1(options) {
  let paused = false;
  let bytesRead = 0;

  const file = fs.createReadStream(fn, options);
  const fileSize = fs.statSync(fn).size;

  assert.strictEqual(file.bytesRead, 0);

  file.on('open', common.mustCall(function(fd) {
    file.length = 0;
    assert.strictEqual(typeof fd, 'number');
    assert.strictEqual(file.bytesRead, 0);
    assert.ok(file.readable);

    // GH-535
    file.pause();
    file.resume();
    file.pause();
    file.resume();
  }));

  file.on('data', function(data) {
    assert.ok(data instanceof Buffer);
    assert.ok(data.byteOffset % 8 === 0);
    assert.ok(!paused);
    file.length += data.length;

    bytesRead += data.length;
    assert.strictEqual(file.bytesRead, bytesRead);

    paused = true;
    file.pause();

    setTimeout(function() {
      paused = false;
      file.resume();
    }, 10);
  });


  file.on('end', common.mustCall(function(chunk) {
    assert.strictEqual(bytesRead, fileSize);
    assert.strictEqual(file.bytesRead, fileSize);
  }));


  file.on('close', common.mustCall(function() {
    assert.strictEqual(bytesRead, fileSize);
    assert.strictEqual(file.bytesRead, fileSize);
  }));

  process.on('exit', function() {
    assert.strictEqual(file.length, 30000);
  });
}

test1({});
test1({
  fs: {
    open: common.mustCall(fs.open),
    read: common.mustCallAtLeast(fs.read, 1),
    close: common.mustCall(fs.close),
  }
});

{
  const file = fs.createReadStream(fn, common.mustNotMutateObjectDeep({ encoding: 'utf8' }));
  file.length = 0;
  file.on('data', function(data) {
    assert.strictEqual(typeof data, 'string');
    file.length += data.length;

    for (let i = 0; i < data.length; i++) {
      // http://www.fileformat.info/info/unicode/char/2026/index.htm
      assert.strictEqual(data[i], '\u2026');
    }
  });

  file.on('close', common.mustCall());

  process.on('exit', function() {
    assert.strictEqual(file.length, 10000);
  });
}

{
  const file =
    fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ bufferSize: 1, start: 1, end: 2 }));
  let contentRead = '';
  file.on('data', function(data) {
    contentRead += data.toString('utf-8');
  });
  file.on('end', common.mustCall(function(data) {
    assert.strictEqual(contentRead, 'yz');
  }));
}

{
  const file = fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ bufferSize: 1, start: 1 }));
  file.data = '';
  file.on('data', function(data) {
    file.data += data.toString('utf-8');
  });
  file.on('end', common.mustCall(function() {
    assert.strictEqual(file.data, 'yz\n');
  }));
}

{
  // Ref: https://github.com/nodejs/node-v0.x-archive/issues/2320
  const file = fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ bufferSize: 1.23, start: 1 }));
  file.data = '';
  file.on('data', function(data) {
    file.data += data.toString('utf-8');
  });
  file.on('end', common.mustCall(function() {
    assert.strictEqual(file.data, 'yz\n');
  }));
}

assert.throws(
  () => {
    fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ start: 10, end: 2 }));
  },
  {
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "start" is out of range. It must be <= "end"' +
             ' (here: 2). Received 10',
    name: 'RangeError'
  });

{
  const stream = fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ start: 0, end: 0 }));
  stream.data = '';

  stream.on('data', function(chunk) {
    stream.data += chunk;
  });

  stream.on('end', common.mustCall(function() {
    assert.strictEqual(stream.data, 'x');
  }));
}

{
  // Verify that end works when start is not specified.
  const stream = new fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ end: 1 }));
  stream.data = '';

  stream.on('data', function(chunk) {
    stream.data += chunk;
  });

  stream.on('end', common.mustCall(function() {
    assert.strictEqual(stream.data, 'xy');
  }));
}

if (!common.isWindows) {
  // Verify that end works when start is not specified, and we do not try to
  // use positioned reads. This makes sure that this keeps working for
  // non-seekable file descriptors.
  tmpdir.refresh();
  const filename = `${tmpdir.path}/foo.pipe`;
  const mkfifoResult = child_process.spawnSync('mkfifo', [filename]);
  if (!mkfifoResult.error) {
    child_process.exec(...common.escapePOSIXShell`echo "xyz foobar" > "${filename}"`);
    const stream = new fs.createReadStream(filename, common.mustNotMutateObjectDeep({ end: 1 }));
    stream.data = '';

    stream.on('data', function(chunk) {
      stream.data += chunk;
    });

    stream.on('end', common.mustCall(function() {
      assert.strictEqual(stream.data, 'xy');
      fs.unlinkSync(filename);
    }));
  } else {
    common.printSkipMessage('mkfifo not available');
  }
}

{
  // Pause and then resume immediately.
  const pauseRes = fs.createReadStream(rangeFile);
  pauseRes.pause();
  pauseRes.resume();
}

{
  let file = fs.createReadStream(rangeFile, common.mustNotMutateObjectDeep({ autoClose: false }));
  let data = '';
  file.on('data', function(chunk) { data += chunk; });
  file.on('end', common.mustCall(function() {
    assert.strictEqual(data, 'xyz\n');
    process.nextTick(function() {
      assert(!file.closed);
      assert(!file.destroyed);
      fileNext();
    });
  }));

  function fileNext() {
    // This will tell us if the fd is usable again or not.
    file = fs.createReadStream(null, common.mustNotMutateObjectDeep({ fd: file.fd, start: 0 }));
    file.data = '';
    file.on('data', function(data) {
      file.data += data;
    });
    file.on('end', common.mustCall(function(err) {
      assert.strictEqual(file.data, 'xyz\n');
    }));
    process.on('exit', function() {
      assert(file.closed);
      assert(file.destroyed);
    });
  }
}

{
  // Just to make sure autoClose won't close the stream because of error.
  const file = fs.createReadStream(null, common.mustNotMutateObjectDeep({ fd: 13337, autoClose: false }));
  file.on('data', common.mustNotCall());
  file.on('error', common.mustCall());
  process.on('exit', function() {
    assert(!file.closed);
    assert(!file.destroyed);
    assert(file.fd);
  });
}

{
  // Make sure stream is destroyed when file does not exist.
  const file = fs.createReadStream('/path/to/file/that/does/not/exist');
  file.on('data', common.mustNotCall());
  file.on('error', common.mustCall());

  process.on('exit', function() {
    assert(file.closed);
    assert(file.destroyed);
  });
}

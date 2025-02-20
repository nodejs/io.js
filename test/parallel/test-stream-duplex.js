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
const Duplex = require('stream').Duplex;
const { Readable, Writable } = require('stream');
const { ReadableStream, WritableStream } = require('stream/web');

const stream = new Duplex({ objectMode: true });

assert(Duplex() instanceof Duplex);
assert(stream._readableState.objectMode);
assert(stream._writableState.objectMode);
assert(stream.allowHalfOpen);
assert.strictEqual(stream.listenerCount('end'), 0);

let written;
let read;

stream._write = (obj, _, cb) => {
  written = obj;
  cb();
};

stream._read = () => {};

stream.on('data', (obj) => {
  read = obj;
});

stream.push({ val: 1 });
stream.end({ val: 2 });

process.on('exit', () => {
  assert.strictEqual(read.val, 1);
  assert.strictEqual(written.val, 2);
});

// Duplex.fromWeb
{
  const dataToRead = Buffer.from('hello');
  const dataToWrite = Buffer.from('world');

  const readable = new ReadableStream({
    start(controller) {
      controller.enqueue(dataToRead);
    },
  });

  const writable = new WritableStream({
    write: common.mustCall((chunk) => {
      assert.strictEqual(chunk, dataToWrite);
    })
  });

  const pair = { readable, writable };
  const duplex = Duplex.fromWeb(pair);

  duplex.write(dataToWrite);
  duplex.once('data', common.mustCall((chunk) => {
    assert.strictEqual(chunk, dataToRead);
  }));
}

// Duplex.fromWeb - using utf8 and objectMode
{
  const dataToRead = 'hello';
  const dataToWrite = 'world';

  const readable = new ReadableStream({
    start(controller) {
      controller.enqueue(dataToRead);
    },
  });

  const writable = new WritableStream({
    write: common.mustCall((chunk) => {
      assert.strictEqual(chunk, dataToWrite);
    })
  });

  const pair = {
    readable,
    writable
  };
  const duplex = Duplex.fromWeb(pair, { encoding: 'utf8', objectMode: true });

  duplex.write(dataToWrite);
  duplex.once('data', common.mustCall((chunk) => {
    assert.strictEqual(chunk, dataToRead);
  }));
}
// Duplex.toWeb
{
  const dataToRead = Buffer.from('hello');
  const dataToWrite = Buffer.from('world');

  const duplex = Duplex({
    read() {
      this.push(dataToRead);
      this.push(null);
    },
    write: common.mustCall((chunk) => {
      assert.strictEqual(chunk, dataToWrite);
    })
  });

  const { writable, readable } = Duplex.toWeb(duplex);
  writable.getWriter().write(dataToWrite);

  readable.getReader().read().then(common.mustCall((result) => {
    assert.deepStrictEqual(Buffer.from(result.value), dataToRead);
  }));
}

const assertMessages = {
  isDuplex: 'Expected a Duplex stream',
  duplexUnchanged: 'Expected the Duplex stream to be returned unchanged',
  notDuplex: 'Expected a non-Duplex stream',
  isReadable: 'Expected a Readable stream'
};

// readable.compose - Duplex stream unchanged
{
  const readable = new Readable({
    read() {}
  });

  const duplex = new Duplex({
    read() {},
    write(chunk, encoding, callback) {
      callback();
    }
  });

  const composedStream = readable.compose(duplex);

  assert(composedStream instanceof Duplex, assertMessages.isDuplex);
  assert.strictEqual(composedStream, duplex, assertMessages.duplexUnchanged);
}

// readable.compose - wraps non-Duplex streams
{
  const readable = new Readable({
    read() {}
  });

  const writable = new Writable({
    write(chunk, encoding, callback) {
      callback();
    }
  });

  const composedStream = readable.compose(writable);

  assert(!(composedStream instanceof Duplex), assertMessages.notDuplex);
  assert(composedStream instanceof Readable, assertMessages.isReadable);
}

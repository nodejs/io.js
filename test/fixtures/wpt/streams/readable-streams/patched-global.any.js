// META: global=window,worker,shadowrealm
'use strict';

// Tests which patch the global environment are kept separate to avoid
// interfering with other tests.

const ReadableStream_prototype_locked_get =
      Object.getOwnPropertyDescriptor(ReadableStream.prototype, 'locked').get;

// Verify that |rs| passes the brand check as a readable stream.
function isReadableStream(rs) {
  try {
    ReadableStream_prototype_locked_get.call(rs);
    return true;
  } catch (e) {
    return false;
  }
}

test(t => {
  const rs = new ReadableStream();

  const trappedProperties = ['highWaterMark', 'size', 'start', 'type', 'mode'];
  for (const property of trappedProperties) {
    // eslint-disable-next-line no-extend-native, accessor-pairs
    Object.defineProperty(Object.prototype, property, {
      get() { throw new Error(`${property} getter called`); },
      configurable: true
    });
  }
  t.add_cleanup(() => {
    for (const property of trappedProperties) {
      delete Object.prototype[property];
    }
  });

  const [branch1, branch2] = rs.tee();
  assert_true(isReadableStream(branch1), 'branch1 should be a ReadableStream');
  assert_true(isReadableStream(branch2), 'branch2 should be a ReadableStream');
}, 'ReadableStream tee() should not touch Object.prototype properties');

test(t => {
  const rs = new ReadableStream();

  const oldReadableStream = self.ReadableStream;

  self.ReadableStream = function() {
    throw new Error('ReadableStream called on global object');
  };

  t.add_cleanup(() => {
    self.ReadableStream = oldReadableStream;
  });

  const [branch1, branch2] = rs.tee();

  assert_true(isReadableStream(branch1), 'branch1 should be a ReadableStream');
  assert_true(isReadableStream(branch2), 'branch2 should be a ReadableStream');
}, 'ReadableStream tee() should not call the global ReadableStream');

promise_test(async t => {
  const rs = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    }
  });

  const oldReadableStreamGetReader = ReadableStream.prototype.getReader;

  const ReadableStreamDefaultReader = (new ReadableStream()).getReader().constructor;
  const oldDefaultReaderRead = ReadableStreamDefaultReader.prototype.read;
  const oldDefaultReaderCancel = ReadableStreamDefaultReader.prototype.cancel;
  const oldDefaultReaderReleaseLock = ReadableStreamDefaultReader.prototype.releaseLock;

  self.ReadableStream.prototype.getReader = function() {
    throw new Error('patched getReader() called');
  };

  ReadableStreamDefaultReader.prototype.read = function() {
    throw new Error('patched read() called');
  };
  ReadableStreamDefaultReader.prototype.cancel = function() {
    throw new Error('patched cancel() called');
  };
  ReadableStreamDefaultReader.prototype.releaseLock = function() {
    throw new Error('patched releaseLock() called');
  };

  t.add_cleanup(() => {
    self.ReadableStream.prototype.getReader = oldReadableStreamGetReader;

    ReadableStreamDefaultReader.prototype.read = oldDefaultReaderRead;
    ReadableStreamDefaultReader.prototype.cancel = oldDefaultReaderCancel;
    ReadableStreamDefaultReader.prototype.releaseLock = oldDefaultReaderReleaseLock;
  });

  // read the first chunk, then cancel
  for await (const chunk of rs) {
    break;
  }

  // should be able to acquire a new reader
  const reader = oldReadableStreamGetReader.call(rs);
  // stream should be cancelled
  await reader.closed;
}, 'ReadableStream async iterator should use the original values of getReader() and ReadableStreamDefaultReader ' +
   'methods');

test(t => {
  const oldPromiseThen = Promise.prototype.then;
  Promise.prototype.then = () => {
    throw new Error('patched then() called');
  };
  t.add_cleanup(() => {
    Promise.prototype.then = oldPromiseThen;
  });
  const [branch1, branch2] = new ReadableStream().tee();
  assert_true(isReadableStream(branch1), 'branch1 should be a ReadableStream');
  assert_true(isReadableStream(branch2), 'branch2 should be a ReadableStream');
}, 'tee() should not call Promise.prototype.then()');

test(t => {
  const oldPromiseThen = Promise.prototype.then;
  Promise.prototype.then = () => {
    throw new Error('patched then() called');
  };
  t.add_cleanup(() => {
    Promise.prototype.then = oldPromiseThen;
  });
  let readableController;
  const rs = new ReadableStream({
    start(c) {
      readableController = c;
    }
  });
  const ws = new WritableStream();
  rs.pipeTo(ws);
  readableController.close();
}, 'pipeTo() should not call Promise.prototype.then()');

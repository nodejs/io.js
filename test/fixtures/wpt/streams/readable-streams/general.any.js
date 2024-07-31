// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-utils.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

test(() => {

  new ReadableStream(); // ReadableStream constructed with no parameters
  new ReadableStream({ }); // ReadableStream constructed with an empty object as parameter
  new ReadableStream({ type: undefined }); // ReadableStream constructed with undefined type
  new ReadableStream(undefined); // ReadableStream constructed with undefined as parameter

  let x;
  new ReadableStream(x); // ReadableStream constructed with an undefined variable as parameter

}, 'ReadableStream can be constructed with no errors');

test(() => {

  assert_throws_js(TypeError, () => new ReadableStream(null), 'constructor should throw when the source is null');

}, 'ReadableStream can\'t be constructed with garbage');

test(() => {

  assert_throws_js(TypeError, () => new ReadableStream({ type: null }),
    'constructor should throw when the type is null');
  assert_throws_js(TypeError, () => new ReadableStream({ type: '' }),
    'constructor should throw when the type is empty string');
  assert_throws_js(TypeError, () => new ReadableStream({ type: 'asdf' }),
    'constructor should throw when the type is asdf');
  assert_throws_exactly(
    error1,
    () => new ReadableStream({ type: { get toString() { throw error1; } } }),
    'constructor should throw when ToString() throws'
  );
  assert_throws_exactly(
    error1,
    () => new ReadableStream({ type: { toString() { throw error1; } } }),
    'constructor should throw when ToString() throws'
  );

}, 'ReadableStream can\'t be constructed with an invalid type');

test(() => {

  assert_throws_js(TypeError, () => {
    new ReadableStream({ start: 'potato' });
  }, 'constructor should throw when start is not a function');

}, 'ReadableStream constructor should throw for non-function start arguments');

test(() => {

  assert_throws_js(TypeError, () => new ReadableStream({ cancel: '2' }), 'constructor should throw');

}, 'ReadableStream constructor will not tolerate initial garbage as cancel argument');

test(() => {

  assert_throws_js(TypeError, () => new ReadableStream({ pull: { } }), 'constructor should throw');

}, 'ReadableStream constructor will not tolerate initial garbage as pull argument');

test(() => {

  let startCalled = false;

  const source = {
    start() {
      assert_equals(this, source, 'source is this during start');
      startCalled = true;
    }
  };

  new ReadableStream(source);
  assert_true(startCalled);

}, 'ReadableStream start should be called with the proper thisArg');

test(() => {

  let startCalled = false;
  const source = {
    start(controller) {
      const properties = ['close', 'constructor', 'desiredSize', 'enqueue', 'error'];
      assert_array_equals(Object.getOwnPropertyNames(Object.getPrototypeOf(controller)).sort(), properties,
        'prototype should have the right properties');

      controller.test = '';
      assert_array_equals(Object.getOwnPropertyNames(Object.getPrototypeOf(controller)).sort(), properties,
        'prototype should still have the right properties');
      assert_not_equals(Object.getOwnPropertyNames(controller).indexOf('test'), -1,
        '"test" should be a property of the controller');

      startCalled = true;
    }
  };

  new ReadableStream(source);
  assert_true(startCalled);

}, 'ReadableStream start controller parameter should be extensible');

test(() => {
  (new ReadableStream()).getReader(undefined);
  (new ReadableStream()).getReader({});
  (new ReadableStream()).getReader({ mode: undefined, notmode: 'ignored' });
  assert_throws_js(TypeError, () => (new ReadableStream()).getReader({ mode: 'potato' }));
}, 'default ReadableStream getReader() should only accept mode:undefined');

promise_test(() => {

  function SimpleStreamSource() {}
  let resolve;
  const promise = new Promise(r => resolve = r);
  SimpleStreamSource.prototype = {
    start: resolve
  };

  new ReadableStream(new SimpleStreamSource());
  return promise;

}, 'ReadableStream should be able to call start method within prototype chain of its source');

promise_test(() => {

  const rs = new ReadableStream({
    start(c) {
      return delay(5).then(() => {
        c.enqueue('a');
        c.close();
      });
    }
  });

  const reader = rs.getReader();
  return reader.read().then(r => {
    assert_object_equals(r, { value: 'a', done: false }, 'value read should be the one enqueued');
    return reader.closed;
  });

}, 'ReadableStream start should be able to return a promise');

promise_test(() => {

  const theError = new Error('rejected!');
  const rs = new ReadableStream({
    start() {
      return delay(1).then(() => {
        throw theError;
      });
    }
  });

  return rs.getReader().closed.then(() => {
    assert_unreached('closed promise should be rejected');
  }, e => {
    assert_equals(e, theError, 'promise should be rejected with the same error');
  });

}, 'ReadableStream start should be able to return a promise and reject it');

promise_test(() => {

  const objects = [
    { potato: 'Give me more!' },
    'test',
    1
  ];

  const rs = new ReadableStream({
    start(c) {
      for (const o of objects) {
        c.enqueue(o);
      }
      c.close();
    }
  });

  const reader = rs.getReader();

  return Promise.all([reader.read(), reader.read(), reader.read(), reader.closed]).then(r => {
    assert_object_equals(r[0], { value: objects[0], done: false }, 'value read should be the one enqueued');
    assert_object_equals(r[1], { value: objects[1], done: false }, 'value read should be the one enqueued');
    assert_object_equals(r[2], { value: objects[2], done: false }, 'value read should be the one enqueued');
  });

}, 'ReadableStream should be able to enqueue different objects.');

promise_test(() => {

  const error = new Error('pull failure');
  const rs = new ReadableStream({
    pull() {
      return Promise.reject(error);
    }
  });

  const reader = rs.getReader();

  let closed = false;
  let read = false;

  return Promise.all([
    reader.closed.then(() => {
      assert_unreached('closed should be rejected');
    }, e => {
      closed = true;
      assert_false(read);
      assert_equals(e, error, 'closed should be rejected with the thrown error');
    }),
    reader.read().then(() => {
      assert_unreached('read() should be rejected');
    }, e => {
      read = true;
      assert_true(closed);
      assert_equals(e, error, 'read() should be rejected with the thrown error');
    })
  ]);

}, 'ReadableStream: if pull rejects, it should error the stream');

promise_test(() => {

  let pullCount = 0;

  new ReadableStream({
    pull() {
      pullCount++;
    }
  });

  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 1, 'pull should be called once start finishes');
    return delay(10);
  }).then(() => {
    assert_equals(pullCount, 1, 'pull should be called exactly once');
  });

}, 'ReadableStream: should only call pull once upon starting the stream');

promise_test(() => {

  let pullCount = 0;

  const rs = new ReadableStream({
    pull(c) {
      // Don't enqueue immediately after start. We want the stream to be empty when we call .read() on it.
      if (pullCount > 0) {
        c.enqueue(pullCount);
      }
      ++pullCount;
    }
  });

  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 1, 'pull should be called once start finishes');
  }).then(() => {
    const reader = rs.getReader();
    const read = reader.read();
    assert_equals(pullCount, 2, 'pull should be called when read is called');
    return read;
  }).then(result => {
    assert_equals(pullCount, 3, 'pull should be called again in reaction to calling read');
    assert_object_equals(result, { value: 1, done: false }, 'the result read should be the one enqueued');
  });

}, 'ReadableStream: should call pull when trying to read from a started, empty stream');

promise_test(() => {

  let pullCount = 0;

  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
    },
    pull() {
      pullCount++;
    }
  });

  const read = rs.getReader().read();
  assert_equals(pullCount, 0, 'calling read() should not cause pull to be called yet');

  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 1, 'pull should be called once start finishes');
    return read;
  }).then(r => {
    assert_object_equals(r, { value: 'a', done: false }, 'first read() should return first chunk');
    assert_equals(pullCount, 1, 'pull should not have been called again');
    return delay(10);
  }).then(() => {
    assert_equals(pullCount, 1, 'pull should be called exactly once');
  });

}, 'ReadableStream: should only call pull once on a non-empty stream read from before start fulfills');

promise_test(() => {

  let pullCount = 0;
  const startPromise = Promise.resolve();

  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
    },
    pull() {
      pullCount++;
    }
  });

  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 0, 'pull should not be called once start finishes, since the queue is full');

    const read = rs.getReader().read();
    assert_equals(pullCount, 1, 'calling read() should cause pull to be called immediately');
    return read;
  }).then(r => {
    assert_object_equals(r, { value: 'a', done: false }, 'first read() should return first chunk');
    return delay(10);
  }).then(() => {
    assert_equals(pullCount, 1, 'pull should be called exactly once');
  });

}, 'ReadableStream: should only call pull once on a non-empty stream read from after start fulfills');

promise_test(() => {

  let pullCount = 0;
  let controller;

  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      ++pullCount;
    }
  });

  const reader = rs.getReader();
  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 1, 'pull should have been called once by the time the stream starts');

    controller.enqueue('a');
    assert_equals(pullCount, 1, 'pull should not have been called again after enqueue');

    return reader.read();
  }).then(() => {
    assert_equals(pullCount, 2, 'pull should have been called again after read');

    return delay(10);
  }).then(() => {
    assert_equals(pullCount, 2, 'pull should be called exactly twice');
  });
}, 'ReadableStream: should call pull in reaction to read()ing the last chunk, if not draining');

promise_test(() => {

  let pullCount = 0;
  let controller;

  const rs = new ReadableStream({
    start(c) {
      controller = c;
    },
    pull() {
      ++pullCount;
    }
  });

  const reader = rs.getReader();

  return flushAsyncEvents().then(() => {
    assert_equals(pullCount, 1, 'pull should have been called once by the time the stream starts');

    controller.enqueue('a');
    assert_equals(pullCount, 1, 'pull should not have been called again after enqueue');

    controller.close();

    return reader.read();
  }).then(() => {
    assert_equals(pullCount, 1, 'pull should not have been called a second time after read');

    return delay(10);
  }).then(() => {
    assert_equals(pullCount, 1, 'pull should be called exactly once');
  });

}, 'ReadableStream: should not call pull() in reaction to read()ing the last chunk, if draining');

promise_test(() => {

  let resolve;
  let returnedPromise;
  let timesCalled = 0;

  const rs = new ReadableStream({
    pull(c) {
      c.enqueue(++timesCalled);
      returnedPromise = new Promise(r => resolve = r);
      return returnedPromise;
    }
  });
  const reader = rs.getReader();

  return reader.read()
  .then(result1 => {
    assert_equals(timesCalled, 1,
      'pull should have been called once after start, but not yet have been called a second time');
    assert_object_equals(result1, { value: 1, done: false }, 'read() should fulfill with the enqueued value');

    return delay(10);
  }).then(() => {
    assert_equals(timesCalled, 1, 'after 10 ms, pull should still only have been called once');

    resolve();
    return returnedPromise;
  }).then(() => {
    assert_equals(timesCalled, 2,
      'after the promise returned by pull is fulfilled, pull should be called a second time');
  });

}, 'ReadableStream: should not call pull until the previous pull call\'s promise fulfills');

promise_test(() => {

  let timesCalled = 0;

  const rs = new ReadableStream(
    {
      start(c) {
        c.enqueue('a');
        c.enqueue('b');
        c.enqueue('c');
      },
      pull() {
        ++timesCalled;
      }
    },
    {
      size() {
        return 1;
      },
      highWaterMark: Infinity
    }
  );
  const reader = rs.getReader();

  return flushAsyncEvents().then(() => {
    return reader.read();
  }).then(result1 => {
    assert_object_equals(result1, { value: 'a', done: false }, 'first chunk should be as expected');

    return reader.read();
  }).then(result2 => {
    assert_object_equals(result2, { value: 'b', done: false }, 'second chunk should be as expected');

    return reader.read();
  }).then(result3 => {
    assert_object_equals(result3, { value: 'c', done: false }, 'third chunk should be as expected');

    return delay(10);
  }).then(() => {
    // Once for after start, and once for every read.
    assert_equals(timesCalled, 4, 'pull() should be called exactly four times');
  });

}, 'ReadableStream: should pull after start, and after every read');

promise_test(() => {

  let timesCalled = 0;
  const startPromise = Promise.resolve();

  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
      c.close();
      return startPromise;
    },
    pull() {
      ++timesCalled;
    }
  });

  const reader = rs.getReader();
  return startPromise.then(() => {
    assert_equals(timesCalled, 0, 'after start finishes, pull should not have been called');

    return reader.read();
  }).then(() => {
    assert_equals(timesCalled, 0, 'reading should not have triggered a pull call');

    return reader.closed;
  }).then(() => {
    assert_equals(timesCalled, 0, 'stream should have closed with still no calls to pull');
  });

}, 'ReadableStream: should not call pull after start if the stream is now closed');

promise_test(() => {

  let timesCalled = 0;
  let resolve;
  const ready = new Promise(r => resolve = r);

  new ReadableStream(
    {
      start() {},
      pull(c) {
        c.enqueue(++timesCalled);

        if (timesCalled === 4) {
          resolve();
        }
      }
    },
    {
      size() {
        return 1;
      },
      highWaterMark: 4
    }
  );

  return ready.then(() => {
    // after start: size = 0, pull()
    // after enqueue(1): size = 1, pull()
    // after enqueue(2): size = 2, pull()
    // after enqueue(3): size = 3, pull()
    // after enqueue(4): size = 4, do not pull
    assert_equals(timesCalled, 4, 'pull() should have been called four times');
  });

}, 'ReadableStream: should call pull after enqueueing from inside pull (with no read requests), if strategy allows');

promise_test(() => {

  let pullCalled = false;

  const rs = new ReadableStream({
    pull(c) {
      pullCalled = true;
      c.close();
    }
  });

  const reader = rs.getReader();
  return reader.closed.then(() => {
    assert_true(pullCalled);
  });

}, 'ReadableStream pull should be able to close a stream.');

promise_test(t => {

  const controllerError = { name: 'controller error' };

  const rs = new ReadableStream({
    pull(c) {
      c.error(controllerError);
    }
  });

  return promise_rejects_exactly(t, controllerError, rs.getReader().closed);

}, 'ReadableStream pull should be able to error a stream.');

promise_test(t => {

  const controllerError = { name: 'controller error' };
  const thrownError = { name: 'thrown error' };

  const rs = new ReadableStream({
    pull(c) {
      c.error(controllerError);
      throw thrownError;
    }
  });

  return promise_rejects_exactly(t, controllerError, rs.getReader().closed);

}, 'ReadableStream pull should be able to error a stream and throw.');

test(() => {

  let startCalled = false;

  new ReadableStream({
    start(c) {
      assert_equals(c.enqueue('a'), undefined, 'the first enqueue should return undefined');
      c.close();

      assert_throws_js(TypeError, () => c.enqueue('b'), 'enqueue after close should throw a TypeError');
      startCalled = true;
    }
  });

  assert_true(startCalled);

}, 'ReadableStream: enqueue should throw when the stream is readable but draining');

test(() => {

  let startCalled = false;

  new ReadableStream({
    start(c) {
      c.close();

      assert_throws_js(TypeError, () => c.enqueue('a'), 'enqueue after close should throw a TypeError');
      startCalled = true;
    }
  });

  assert_true(startCalled);

}, 'ReadableStream: enqueue should throw when the stream is closed');

promise_test(() => {

  let startCalled = 0;
  let pullCalled = 0;
  let cancelCalled = 0;

  /* eslint-disable no-use-before-define */
  class Source {
    start(c) {
      startCalled++;
      assert_equals(this, theSource, 'start() should be called with the correct this');
      c.enqueue('a');
    }

    pull() {
      pullCalled++;
      assert_equals(this, theSource, 'pull() should be called with the correct this');
    }

    cancel() {
      cancelCalled++;
      assert_equals(this, theSource, 'cancel() should be called with the correct this');
    }
  }
  /* eslint-enable no-use-before-define */

  const theSource = new Source();
  theSource.debugName = 'the source object passed to the constructor'; // makes test failures easier to diagnose

  const rs = new ReadableStream(theSource);
  const reader = rs.getReader();

  return reader.read().then(() => {
    reader.releaseLock();
    rs.cancel();
    assert_equals(startCalled, 1);
    assert_equals(pullCalled, 1);
    assert_equals(cancelCalled, 1);
    return rs.getReader().closed;
  });

}, 'ReadableStream: should call underlying source methods as methods');

test(() => {
  new ReadableStream({
    start(c) {
      assert_equals(c.desiredSize, 10, 'desiredSize must start at highWaterMark');
      c.close();
      assert_equals(c.desiredSize, 0, 'after closing, desiredSize must be 0');
    }
  }, {
    highWaterMark: 10
  });
}, 'ReadableStream: desiredSize when closed');

test(() => {
  new ReadableStream({
    start(c) {
      assert_equals(c.desiredSize, 10, 'desiredSize must start at highWaterMark');
      c.error();
      assert_equals(c.desiredSize, null, 'after erroring, desiredSize must be null');
    }
  }, {
    highWaterMark: 10
  });
}, 'ReadableStream: desiredSize when errored');

test(() => {
  class Subclass extends ReadableStream {
    extraFunction() {
      return true;
    }
  }
  assert_equals(
      Object.getPrototypeOf(Subclass.prototype), ReadableStream.prototype,
      'Subclass.prototype\'s prototype should be ReadableStream.prototype');
  assert_equals(Object.getPrototypeOf(Subclass), ReadableStream,
                'Subclass\'s prototype should be ReadableStream');
  const sub = new Subclass();
  assert_true(sub instanceof ReadableStream,
              'Subclass object should be an instance of ReadableStream');
  assert_true(sub instanceof Subclass,
              'Subclass object should be an instance of Subclass');
  const lockedGetter = Object.getOwnPropertyDescriptor(
      ReadableStream.prototype, 'locked').get;
  assert_equals(lockedGetter.call(sub), sub.locked,
                'Subclass object should pass brand check');
  assert_true(sub.extraFunction(),
              'extraFunction() should be present on Subclass object');
}, 'Subclassing ReadableStream should work');

test(() => {

  let startCalled = false;
  new ReadableStream({
    start(c) {
      assert_equals(c.desiredSize, 1);
      c.enqueue('a');
      assert_equals(c.desiredSize, 0);
      c.enqueue('b');
      assert_equals(c.desiredSize, -1);
      c.enqueue('c');
      assert_equals(c.desiredSize, -2);
      c.enqueue('d');
      assert_equals(c.desiredSize, -3);
      c.enqueue('e');
      startCalled = true;
    }
  });

  assert_true(startCalled);

}, 'ReadableStream strategies: the default strategy should give desiredSize of 1 to start, decreasing by 1 per enqueue');

promise_test(() => {

  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  });
  const reader = rs.getReader();

  assert_equals(controller.desiredSize, 1, 'desiredSize should start at 1');
  controller.enqueue('a');
  assert_equals(controller.desiredSize, 0, 'desiredSize should decrease to 0 after first enqueue');

  return reader.read().then(result1 => {
    assert_object_equals(result1, { value: 'a', done: false }, 'first chunk read should be correct');

    assert_equals(controller.desiredSize, 1, 'desiredSize should go up to 1 after the first read');
    controller.enqueue('b');
    assert_equals(controller.desiredSize, 0, 'desiredSize should go down to 0 after the second enqueue');

    return reader.read();
  }).then(result2 => {
    assert_object_equals(result2, { value: 'b', done: false }, 'second chunk read should be correct');

    assert_equals(controller.desiredSize, 1, 'desiredSize should go up to 1 after the second read');
    controller.enqueue('c');
    assert_equals(controller.desiredSize, 0, 'desiredSize should go down to 0 after the third enqueue');

    return reader.read();
  }).then(result3 => {
    assert_object_equals(result3, { value: 'c', done: false }, 'third chunk read should be correct');

    assert_equals(controller.desiredSize, 1, 'desiredSize should go up to 1 after the third read');
    controller.enqueue('d');
    assert_equals(controller.desiredSize, 0, 'desiredSize should go down to 0 after the fourth enqueue');
  });

}, 'ReadableStream strategies: the default strategy should continue giving desiredSize of 1 if the chunks are read immediately');

promise_test(t => {

  const randomSource = new RandomPushSource(8);

  const rs = new ReadableStream({
    start(c) {
      assert_equals(typeof c, 'object', 'c should be an object in start');
      assert_equals(typeof c.enqueue, 'function', 'enqueue should be a function in start');
      assert_equals(typeof c.close, 'function', 'close should be a function in start');
      assert_equals(typeof c.error, 'function', 'error should be a function in start');

      randomSource.ondata = t.step_func(chunk => {
        if (!c.enqueue(chunk) <= 0) {
          randomSource.readStop();
        }
      });

      randomSource.onend = c.close.bind(c);
      randomSource.onerror = c.error.bind(c);
    },

    pull(c) {
      assert_equals(typeof c, 'object', 'c should be an object in pull');
      assert_equals(typeof c.enqueue, 'function', 'enqueue should be a function in pull');
      assert_equals(typeof c.close, 'function', 'close should be a function in pull');

      randomSource.readStart();
    }
  });

  return readableStreamToArray(rs).then(chunks => {
    assert_equals(chunks.length, 8, '8 chunks should be read');
    for (const chunk of chunks) {
      assert_equals(chunk.length, 128, 'chunk should have 128 bytes');
    }
  });

}, 'ReadableStream integration test: adapting a random push source');

promise_test(() => {

  const rs = sequentialReadableStream(10);

  return readableStreamToArray(rs).then(chunks => {
    assert_true(rs.source.closed, 'source should be closed after all chunks are read');
    assert_array_equals(chunks, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 'the expected 10 chunks should be read');
  });

}, 'ReadableStream integration test: adapting a sync pull source');

promise_test(() => {

  const rs = sequentialReadableStream(10, { async: true });

  return readableStreamToArray(rs).then(chunks => {
    assert_true(rs.source.closed, 'source should be closed after all chunks are read');
    assert_array_equals(chunks, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 'the expected 10 chunks should be read');
  });

}, 'ReadableStream integration test: adapting an async pull source');

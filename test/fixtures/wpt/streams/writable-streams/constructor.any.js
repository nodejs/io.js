// META: global=window,worker,shadowrealm
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

promise_test(() => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    }
  });

  // Now error the stream after its construction.
  controller.error(error1);

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, null, 'desiredSize should be null');
  return writer.closed.catch(r => {
    assert_equals(r, error1, 'ws should be errored by the passed error');
  });
}, 'controller argument should be passed to start method');

promise_test(t => {
  const ws = new WritableStream({
    write(chunk, controller) {
      controller.error(error1);
    }
  });

  const writer = ws.getWriter();

  return Promise.all([
    writer.write('a'),
    promise_rejects_exactly(t, error1, writer.closed, 'controller.error() in write() should error the stream')
  ]);
}, 'controller argument should be passed to write method');

// Older versions of the standard had the controller argument passed to close(). It wasn't useful, and so has been
// removed. This test remains to identify implementations that haven't been updated.
promise_test(t => {
  const ws = new WritableStream({
    close(...args) {
      t.step(() => {
        assert_array_equals(args, [], 'no arguments should be passed to close');
      });
    }
  });

  return ws.getWriter().close();
}, 'controller argument should not be passed to close method');

promise_test(() => {
  const ws = new WritableStream({}, {
    highWaterMark: 1000,
    size() { return 1; }
  });

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, 1000, 'desiredSize should be 1000');
  return writer.ready.then(v => {
    assert_equals(v, undefined, 'ready promise should fulfill with undefined');
  });
}, 'highWaterMark should be reflected to desiredSize');

promise_test(() => {
  const ws = new WritableStream({}, {
    highWaterMark: Infinity,
    size() { return 0; }
  });

  const writer = ws.getWriter();

  assert_equals(writer.desiredSize, Infinity, 'desiredSize should be Infinity');

  return writer.ready;
}, 'WritableStream should be writable and ready should fulfill immediately if the strategy does not apply ' +
    'backpressure');

test(() => {
  new WritableStream();
}, 'WritableStream should be constructible with no arguments');

test(() => {
  const underlyingSink = { get start() { throw error1; } };
  const queuingStrategy = { highWaterMark: 0, get size() { throw error2; } };

  // underlyingSink is converted in prose in the method body, whereas queuingStrategy is done at the IDL layer.
  // So the queuingStrategy exception should be encountered first.
  assert_throws_exactly(error2, () => new WritableStream(underlyingSink, queuingStrategy));
}, 'underlyingSink argument should be converted after queuingStrategy argument');

test(() => {
  const ws = new WritableStream({});

  const writer = ws.getWriter();

  assert_equals(typeof writer.write, 'function', 'writer should have a write method');
  assert_equals(typeof writer.abort, 'function', 'writer should have an abort method');
  assert_equals(typeof writer.close, 'function', 'writer should have a close method');

  assert_equals(writer.desiredSize, 1, 'desiredSize should start at 1');

  assert_not_equals(typeof writer.ready, 'undefined', 'writer should have a ready property');
  assert_equals(typeof writer.ready.then, 'function', 'ready property should be thenable');
  assert_not_equals(typeof writer.closed, 'undefined', 'writer should have a closed property');
  assert_equals(typeof writer.closed.then, 'function', 'closed property should be thenable');
}, 'WritableStream instances should have standard methods and properties');

test(() => {
  let WritableStreamDefaultController;
  new WritableStream({
    start(c) {
      WritableStreamDefaultController = c.constructor;
    }
  });

  assert_throws_js(TypeError, () => new WritableStreamDefaultController({}),
                   'constructor should throw a TypeError exception');
}, 'WritableStreamDefaultController constructor should throw');

test(() => {
  let WritableStreamDefaultController;
  const stream = new WritableStream({
    start(c) {
      WritableStreamDefaultController = c.constructor;
    }
  });

  assert_throws_js(TypeError, () => new WritableStreamDefaultController(stream),
                   'constructor should throw a TypeError exception');
}, 'WritableStreamDefaultController constructor should throw when passed an initialised WritableStream');

test(() => {
  const stream = new WritableStream();
  const writer = stream.getWriter();
  const WritableStreamDefaultWriter = writer.constructor;
  writer.releaseLock();
  assert_throws_js(TypeError, () => new WritableStreamDefaultWriter({}),
                   'constructor should throw a TypeError exception');
}, 'WritableStreamDefaultWriter should throw unless passed a WritableStream');

test(() => {
  const stream = new WritableStream();
  const writer = stream.getWriter();
  const WritableStreamDefaultWriter = writer.constructor;
  assert_throws_js(TypeError, () => new WritableStreamDefaultWriter(stream),
                   'constructor should throw a TypeError exception');
}, 'WritableStreamDefaultWriter constructor should throw when stream argument is locked');

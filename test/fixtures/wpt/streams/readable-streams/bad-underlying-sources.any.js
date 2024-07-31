// META: global=window,worker,shadowrealm
'use strict';


test(() => {

  const theError = new Error('a unique string');

  assert_throws_exactly(theError, () => {
    new ReadableStream({
      get start() {
        throw theError;
      }
    });
  }, 'constructing the stream should re-throw the error');

}, 'Underlying source start: throwing getter');


test(() => {

  const theError = new Error('a unique string');

  assert_throws_exactly(theError, () => {
    new ReadableStream({
      start() {
        throw theError;
      }
    });
  }, 'constructing the stream should re-throw the error');

}, 'Underlying source start: throwing method');


test(() => {

  const theError = new Error('a unique string');
  assert_throws_exactly(theError, () => new ReadableStream({
    get pull() {
      throw theError;
    }
  }), 'constructor should throw');

}, 'Underlying source: throwing pull getter (initial pull)');


promise_test(t => {

  const theError = new Error('a unique string');
  const rs = new ReadableStream({
    pull() {
      throw theError;
    }
  });

  return promise_rejects_exactly(t, theError, rs.getReader().closed);

}, 'Underlying source: throwing pull method (initial pull)');


promise_test(t => {

  const theError = new Error('a unique string');

  let counter = 0;
  const rs = new ReadableStream({
    get pull() {
      ++counter;
      if (counter === 1) {
        return c => c.enqueue('a');
      }

      throw theError;
    }
  });
  const reader = rs.getReader();

  return Promise.all([
    reader.read().then(r => {
      assert_object_equals(r, { value: 'a', done: false }, 'the first chunk read should be correct');
    }),
    reader.read().then(r => {
      assert_object_equals(r, { value: 'a', done: false }, 'the second chunk read should be correct');
      assert_equals(counter, 1, 'counter should be 1');
    })
  ]);

}, 'Underlying source pull: throwing getter (second pull does not result in a second get)');

promise_test(t => {

  const theError = new Error('a unique string');

  let counter = 0;
  const rs = new ReadableStream({
    pull(c) {
      ++counter;
      if (counter === 1) {
        c.enqueue('a');
        return;
      }

      throw theError;
    }
  });
  const reader = rs.getReader();

  return Promise.all([
    reader.read().then(r => {
      assert_object_equals(r, { value: 'a', done: false }, 'the chunk read should be correct');
    }),
    promise_rejects_exactly(t, theError, reader.closed)
  ]);

}, 'Underlying source pull: throwing method (second pull)');

test(() => {

  const theError = new Error('a unique string');
  assert_throws_exactly(theError, () => new ReadableStream({
    get cancel() {
      throw theError;
    }
  }), 'constructor should throw');

}, 'Underlying source cancel: throwing getter');

promise_test(t => {

  const theError = new Error('a unique string');
  const rs = new ReadableStream({
    cancel() {
      throw theError;
    }
  });

  return promise_rejects_exactly(t, theError, rs.cancel());

}, 'Underlying source cancel: throwing method');

promise_test(() => {

  let controller;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
    }
  });

  rs.cancel();
  assert_throws_js(TypeError, () => controller.enqueue('a'), 'Calling enqueue after canceling should throw');

  return rs.getReader().closed;

}, 'Underlying source: calling enqueue on an empty canceled stream should throw');

promise_test(() => {

  let controller;
  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
      c.enqueue('b');
      controller = c;
    }
  });

  rs.cancel();
  assert_throws_js(TypeError, () => controller.enqueue('c'), 'Calling enqueue after canceling should throw');

  return rs.getReader().closed;

}, 'Underlying source: calling enqueue on a non-empty canceled stream should throw');

promise_test(() => {

  return new ReadableStream({
    start(c) {
      c.close();
      assert_throws_js(TypeError, () => c.enqueue('a'), 'call to enqueue should throw a TypeError');
    }
  }).getReader().closed;

}, 'Underlying source: calling enqueue on a closed stream should throw');

promise_test(t => {

  const theError = new Error('boo');
  const closed = new ReadableStream({
    start(c) {
      c.error(theError);
      assert_throws_js(TypeError, () => c.enqueue('a'), 'call to enqueue should throw the error');
    }
  }).getReader().closed;

  return promise_rejects_exactly(t, theError, closed);

}, 'Underlying source: calling enqueue on an errored stream should throw');

promise_test(() => {

  return new ReadableStream({
    start(c) {
      c.close();
      assert_throws_js(TypeError, () => c.close(), 'second call to close should throw a TypeError');
    }
  }).getReader().closed;

}, 'Underlying source: calling close twice on an empty stream should throw the second time');

promise_test(() => {

  let startCalled = false;
  let readCalled = false;
  const reader = new ReadableStream({
    start(c) {
      c.enqueue('a');
      c.close();
      assert_throws_js(TypeError, () => c.close(), 'second call to close should throw a TypeError');
      startCalled = true;
    }
  }).getReader();

  return Promise.all([
    reader.read().then(r => {
      assert_object_equals(r, { value: 'a', done: false }, 'read() should read the enqueued chunk');
      readCalled = true;
    }),
    reader.closed.then(() => {
      assert_true(startCalled);
      assert_true(readCalled);
    })
  ]);

}, 'Underlying source: calling close twice on a non-empty stream should throw the second time');

promise_test(() => {

  let controller;
  let startCalled = false;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
      startCalled = true;
    }
  });

  rs.cancel();
  assert_throws_js(TypeError, () => controller.close(), 'Calling close after canceling should throw');

  return rs.getReader().closed.then(() => {
    assert_true(startCalled);
  });

}, 'Underlying source: calling close on an empty canceled stream should throw');

promise_test(() => {

  let controller;
  let startCalled = false;
  const rs = new ReadableStream({
    start(c) {
      controller = c;
      c.enqueue('a');
      startCalled = true;
    }
  });

  rs.cancel();
  assert_throws_js(TypeError, () => controller.close(), 'Calling close after canceling should throw');

  return rs.getReader().closed.then(() => {
    assert_true(startCalled);
  });

}, 'Underlying source: calling close on a non-empty canceled stream should throw');

promise_test(() => {

  const theError = new Error('boo');
  let startCalled = false;

  const closed = new ReadableStream({
    start(c) {
      c.error(theError);
      assert_throws_js(TypeError, () => c.close(), 'call to close should throw a TypeError');
      startCalled = true;
    }
  }).getReader().closed;

  return closed.catch(e => {
    assert_true(startCalled);
    assert_equals(e, theError, 'closed should reject with the error');
  });

}, 'Underlying source: calling close after error should throw');

promise_test(() => {

  const theError = new Error('boo');
  let startCalled = false;

  const closed = new ReadableStream({
    start(c) {
      c.error(theError);
      c.error();
      startCalled = true;
    }
  }).getReader().closed;

  return closed.catch(e => {
    assert_true(startCalled);
    assert_equals(e, theError, 'closed should reject with the error');
  });

}, 'Underlying source: calling error twice should not throw');

promise_test(() => {

  let startCalled = false;

  const closed = new ReadableStream({
    start(c) {
      c.close();
      c.error();
      startCalled = true;
    }
  }).getReader().closed;

  return closed.then(() => assert_true(startCalled));

}, 'Underlying source: calling error after close should not throw');

promise_test(() => {

  let startCalled = false;
  const firstError = new Error('1');
  const secondError = new Error('2');

  const closed = new ReadableStream({
    start(c) {
      c.error(firstError);
      startCalled = true;
      return Promise.reject(secondError);
    }
  }).getReader().closed;

  return closed.catch(e => {
    assert_true(startCalled);
    assert_equals(e, firstError, 'closed should reject with the first error');
  });

}, 'Underlying source: calling error and returning a rejected promise from start should cause the stream to error ' +
   'with the first error');

promise_test(() => {

  let startCalled = false;
  const firstError = new Error('1');
  const secondError = new Error('2');

  const closed = new ReadableStream({
    pull(c) {
      c.error(firstError);
      startCalled = true;
      return Promise.reject(secondError);
    }
  }).getReader().closed;

  return closed.catch(e => {
    assert_true(startCalled);
    assert_equals(e, firstError, 'closed should reject with the first error');
  });

}, 'Underlying source: calling error and returning a rejected promise from pull should cause the stream to error ' +
   'with the first error');

const error1 = { name: 'error1' };

promise_test(t => {

  let pullShouldThrow = false;
  const rs = new ReadableStream({
    pull(controller) {
      if (pullShouldThrow) {
        throw error1;
      }
      controller.enqueue(0);
    }
  }, new CountQueuingStrategy({highWaterMark: 1}));
  const reader = rs.getReader();
  return Promise.resolve().then(() => {
    pullShouldThrow = true;
    return Promise.all([
      reader.read(),
      promise_rejects_exactly(t, error1, reader.closed, '.closed promise should reject')
    ]);
  });

}, 'read should not error if it dequeues and pull() throws');

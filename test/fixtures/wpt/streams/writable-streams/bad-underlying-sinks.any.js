// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

test(() => {
  assert_throws_exactly(error1, () => {
    new WritableStream({
      get start() {
        throw error1;
      }
    });
  }, 'constructor should throw same error as throwing start getter');

  assert_throws_exactly(error1, () => {
    new WritableStream({
      start() {
        throw error1;
      }
    });
  }, 'constructor should throw same error as throwing start method');

  assert_throws_js(TypeError, () => {
    new WritableStream({
      start: 'not a function or undefined'
    });
  }, 'constructor should throw TypeError when passed a non-function start property');

  assert_throws_js(TypeError, () => {
    new WritableStream({
      start: { apply() {} }
    });
  }, 'constructor should throw TypeError when passed a non-function start property with an .apply method');
}, 'start: errors in start cause WritableStream constructor to throw');

promise_test(t => {

  const ws = recordingWritableStream({
    close() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.close(), 'close() promise must reject with the thrown error')
  .then(() => promise_rejects_exactly(t, error1, writer.ready, 'ready promise must reject with the thrown error'))
  .then(() => promise_rejects_exactly(t, error1, writer.closed, 'closed promise must reject with the thrown error'))
  .then(() => {
    assert_array_equals(ws.events, ['close']);
  });

}, 'close: throwing method should cause writer close() and ready to reject');

promise_test(t => {

  const ws = recordingWritableStream({
    close() {
      return Promise.reject(error1);
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.close(), 'close() promise must reject with the same error')
  .then(() => promise_rejects_exactly(t, error1, writer.ready, 'ready promise must reject with the same error'))
  .then(() => assert_array_equals(ws.events, ['close']));

}, 'close: returning a rejected promise should cause writer close() and ready to reject');

test(() => {
  assert_throws_exactly(error1, () => new WritableStream({
    get close() {
      throw error1;
    }
  }), 'constructor should throw');
}, 'close: throwing getter should cause constructor to throw');

test(() => {
  assert_throws_exactly(error1, () => new WritableStream({
    get write() {
      throw error1;
    }
  }), 'constructor should throw');
}, 'write: throwing getter should cause write() and closed to reject');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.write('a'), 'write should reject with the thrown error')
  .then(() => promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with the thrown error'));
}, 'write: throwing method should cause write() and closed to reject');

promise_test(t => {

  let rejectSinkWritePromise;
  const ws = recordingWritableStream({
    write() {
      return new Promise((r, reject) => {
        rejectSinkWritePromise = reject;
      });
    }
  });

  return flushAsyncEvents().then(() => {
    const writer = ws.getWriter();
    const writePromise = writer.write('a');
    rejectSinkWritePromise(error1);

    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise, 'writer write must reject with the same error'),
      promise_rejects_exactly(t, error1, writer.ready, 'ready promise must reject with the same error')
    ]);
  })
  .then(() => {
    assert_array_equals(ws.events, ['write', 'a']);
  });

}, 'write: returning a promise that becomes rejected after the writer write() should cause writer write() and ready ' +
   'to reject');

promise_test(t => {

  const ws = recordingWritableStream({
    write() {
      if (ws.events.length === 2) {
        return delay(0);
      }

      return Promise.reject(error1);
    }
  });

  const writer = ws.getWriter();

  // Do not wait for this; we want to test the ready promise when the stream is "full" (desiredSize = 0), but if we wait
  // then the stream will transition back to "empty" (desiredSize = 1)
  writer.write('a');
  const readyPromise = writer.ready;

  return promise_rejects_exactly(t, error1, writer.write('b'), 'second write must reject with the same error').then(() => {
    assert_equals(writer.ready, readyPromise,
      'the ready promise must not change, since the queue was full after the first write, so the pending one simply ' +
      'transitioned');
    return promise_rejects_exactly(t, error1, writer.ready, 'ready promise must reject with the same error');
  })
  .then(() => assert_array_equals(ws.events, ['write', 'a', 'write', 'b']));

}, 'write: returning a rejected promise (second write) should cause writer write() and ready to reject');

test(() => {
  assert_throws_js(TypeError, () => new WritableStream({
    start: 'test'
  }), 'constructor should throw');
}, 'start: non-function start method');

test(() => {
  assert_throws_js(TypeError, () => new WritableStream({
    write: 'test'
  }), 'constructor should throw');
}, 'write: non-function write method');

test(() => {
  assert_throws_js(TypeError, () => new WritableStream({
    close: 'test'
  }), 'constructor should throw');
}, 'close: non-function close method');

test(() => {
  assert_throws_js(TypeError, () => new WritableStream({
    abort: { apply() {} }
  }), 'constructor should throw');
}, 'abort: non-function abort method with .apply');

test(() => {
  assert_throws_exactly(error1, () => new WritableStream({
    get abort() {
      throw error1;
    }
  }), 'constructor should throw');
}, 'abort: throwing getter should cause abort() and closed to reject');

promise_test(t => {
  const abortReason = new Error('different string');
  const ws = new WritableStream({
    abort() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.abort(abortReason), 'abort should reject with the thrown error')
  .then(() => promise_rejects_exactly(t, abortReason, writer.closed, 'closed should reject with abortReason'));
}, 'abort: throwing method should cause abort() and closed to reject');

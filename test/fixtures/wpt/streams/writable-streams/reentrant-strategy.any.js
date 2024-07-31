// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

// These tests exercise the pathological case of calling WritableStream* methods from within the strategy.size()
// callback. This is not something any real code should ever do. Failures here indicate subtle deviations from the
// standard that may affect real, non-pathological code.

const error1 = { name: 'error1' };

promise_test(() => {
  let writer;
  const strategy = {
    size(chunk) {
      if (chunk > 0) {
        writer.write(chunk - 1);
      }
      return chunk;
    }
  };

  const ws = recordingWritableStream({}, strategy);
  writer = ws.getWriter();
  return writer.write(2)
      .then(() => {
        assert_array_equals(ws.events, ['write', 0, 'write', 1, 'write', 2], 'writes should appear in order');
      });
}, 'writes should be written in the standard order');

promise_test(() => {
  let writer;
  const events = [];
  const strategy = {
    size(chunk) {
      events.push('size', chunk);
      if (chunk > 0) {
        writer.write(chunk - 1)
            .then(() => events.push('writer.write done', chunk - 1));
      }
      return chunk;
    }
  };
  const ws = new WritableStream({
    write(chunk) {
      events.push('sink.write', chunk);
    }
  }, strategy);
  writer = ws.getWriter();
  return writer.write(2)
      .then(() => events.push('writer.write done', 2))
      .then(() => flushAsyncEvents())
      .then(() => {
        assert_array_equals(events, ['size', 2, 'size', 1, 'size', 0,
                                     'sink.write', 0, 'sink.write', 1, 'writer.write done', 0,
                                     'sink.write', 2, 'writer.write done', 1,
                                     'writer.write done', 2],
                            'events should happen in standard order');
      });
}, 'writer.write() promises should resolve in the standard order');

promise_test(t => {
  let controller;
  const strategy = {
    size() {
      controller.error(error1);
      return 1;
    }
  };
  const ws = recordingWritableStream({
    start(c) {
      controller = c;
    }
  }, strategy);
  const resolved = [];
  const writer = ws.getWriter();
  const readyPromise1 = writer.ready.then(() => resolved.push('ready1'));
  const writePromise = promise_rejects_exactly(t, error1, writer.write(),
                                               'write() should reject with the error')
                                                   .then(() => resolved.push('write'));
  const readyPromise2 = promise_rejects_exactly(t, error1, writer.ready, 'ready should reject with error1')
      .then(() => resolved.push('ready2'));
  const closedPromise = promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with error1')
      .then(() => resolved.push('closed'));
  return Promise.all([readyPromise1, writePromise, readyPromise2, closedPromise])
      .then(() => {
        assert_array_equals(resolved, ['ready1', 'write', 'ready2', 'closed'],
                            'promises should resolve in standard order');
        assert_array_equals(ws.events, [], 'underlying sink write should not be called');
      });
}, 'controller.error() should work when called from within strategy.size()');

promise_test(t => {
  let writer;
  const strategy = {
    size() {
      writer.close();
      return 1;
    }
  };

  const ws = recordingWritableStream({}, strategy);
  writer = ws.getWriter();
  return promise_rejects_js(t, TypeError, writer.write('a'), 'write() promise should reject')
      .then(() => {
        assert_array_equals(ws.events, ['close'], 'sink.write() should not be called');
      });
}, 'close() should work when called from within strategy.size()');

promise_test(t => {
  let writer;
  const strategy = {
    size() {
      writer.abort(error1);
      return 1;
    }
  };

  const ws = recordingWritableStream({}, strategy);
  writer = ws.getWriter();
  return promise_rejects_exactly(t, error1, writer.write('a'), 'write() promise should reject')
      .then(() => {
        assert_array_equals(ws.events, ['abort', error1], 'sink.write() should not be called');
      });
}, 'abort() should work when called from within strategy.size()');

promise_test(t => {
  let writer;
  const strategy = {
    size() {
      writer.releaseLock();
      return 1;
    }
  };

  const ws = recordingWritableStream({}, strategy);
  writer = ws.getWriter();
  const writePromise = promise_rejects_js(t, TypeError, writer.write('a'), 'write() promise should reject');
  const readyPromise = promise_rejects_js(t, TypeError, writer.ready, 'ready promise should reject');
  const closedPromise = promise_rejects_js(t, TypeError, writer.closed, 'closed promise should reject');
  return Promise.all([writePromise, readyPromise, closedPromise])
      .then(() => {
        assert_array_equals(ws.events, [], 'sink.write() should not be called');
      });
}, 'releaseLock() should abort the write() when called within strategy.size()');

promise_test(t => {
  let writer1;
  let ws;
  let writePromise2;
  let closePromise;
  let closedPromise2;
  const strategy = {
    size(chunk) {
      if (chunk > 0) {
        writer1.releaseLock();
        const writer2 = ws.getWriter();
        writePromise2 = writer2.write(0);
        closePromise = writer2.close();
        closedPromise2 = writer2.closed;
      }
      return 1;
    }
  };
  ws = recordingWritableStream({}, strategy);
  writer1 = ws.getWriter();
  const writePromise1 = promise_rejects_js(t, TypeError, writer1.write(1), 'write() promise should reject');
  const readyPromise = promise_rejects_js(t, TypeError, writer1.ready, 'ready promise should reject');
  const closedPromise1 = promise_rejects_js(t, TypeError, writer1.closed, 'closed promise should reject');
  return Promise.all([writePromise1, readyPromise, closedPromise1, writePromise2, closePromise, closedPromise2])
      .then(() => {
        assert_array_equals(ws.events, ['write', 0, 'close'], 'sink.write() should only be called once');
      });
}, 'original reader should error when new reader is created within strategy.size()');

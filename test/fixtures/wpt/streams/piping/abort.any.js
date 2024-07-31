// META: global=window,worker,shadowrealm
// META: script=../resources/recording-streams.js
// META: script=../resources/test-utils.js
'use strict';

// Tests for the use of pipeTo with AbortSignal.
// There is some extra complexity to avoid timeouts in environments where abort is not implemented.

const error1 = new Error('error1');
error1.name = 'error1';
const error2 = new Error('error2');
error2.name = 'error2';

const errorOnPull = {
  pull(controller) {
    // This will cause the test to error if pipeTo abort is not implemented.
    controller.error('failed to abort');
  }
};

// To stop pull() being called immediately when the stream is created, we need to set highWaterMark to 0.
const hwm0 = { highWaterMark: 0 };

for (const invalidSignal of [null, 'AbortSignal', true, -1, Object.create(AbortSignal.prototype)]) {
  promise_test(t => {
    const rs = recordingReadableStream(errorOnPull, hwm0);
    const ws = recordingWritableStream();
    return promise_rejects_js(t, TypeError, rs.pipeTo(ws, { signal: invalidSignal }), 'pipeTo should reject')
        .then(() => {
          assert_equals(rs.events.length, 0, 'no ReadableStream methods should have been called');
          assert_equals(ws.events.length, 0, 'no WritableStream methods should have been called');
        });
  }, `a signal argument '${invalidSignal}' should cause pipeTo() to reject`);
}

promise_test(t => {
  const rs = recordingReadableStream(errorOnPull, hwm0);
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal }), 'pipeTo should reject')
      .then(() => Promise.all([
        rs.getReader().closed,
        promise_rejects_dom(t, 'AbortError', ws.getWriter().closed, 'writer.closed should reject')
      ]))
      .then(() => {
        assert_equals(rs.events.length, 2, 'cancel should have been called');
        assert_equals(rs.events[0], 'cancel', 'first event should be cancel');
        assert_equals(rs.events[1].name, 'AbortError', 'the argument to cancel should be an AbortError');
        assert_equals(rs.events[1].constructor.name, 'DOMException',
                      'the argument to cancel should be a DOMException');
      });
}, 'an aborted signal should cause the writable stream to reject with an AbortError');

for (const reason of [null, undefined, error1]) {
  promise_test(async t => {
    const rs = recordingReadableStream(errorOnPull, hwm0);
    const ws = new WritableStream();
    const abortController = new AbortController();
    const signal = abortController.signal;
    abortController.abort(reason);
    const pipeToPromise = rs.pipeTo(ws, { signal });
    if (reason !== undefined) {
      await promise_rejects_exactly(t, reason, pipeToPromise, 'pipeTo rejects with abort reason');
    } else {
      await promise_rejects_dom(t, 'AbortError', pipeToPromise, 'pipeTo rejects with AbortError');
    }
    const error = await pipeToPromise.catch(e => e);
    await rs.getReader().closed;
    await promise_rejects_exactly(t, error, ws.getWriter().closed, 'the writable should be errored with the same object');
    assert_equals(signal.reason, error, 'signal.reason should be error'),
    assert_equals(rs.events.length, 2, 'cancel should have been called');
    assert_equals(rs.events[0], 'cancel', 'first event should be cancel');
    assert_equals(rs.events[1], error, 'the readable should be canceled with the same object');
  }, `(reason: '${reason}') all the error objects should be the same object`);
}

promise_test(t => {
  const rs = recordingReadableStream(errorOnPull, hwm0);
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal, preventCancel: true }), 'pipeTo should reject')
      .then(() => assert_equals(rs.events.length, 0, 'cancel should not be called'));
}, 'preventCancel should prevent canceling the readable');

promise_test(t => {
  const rs = new ReadableStream(errorOnPull, hwm0);
  const ws = recordingWritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal, preventAbort: true }), 'pipeTo should reject')
      .then(() => {
        assert_equals(ws.events.length, 0, 'writable should not have been aborted');
        return ws.getWriter().ready;
      });
}, 'preventAbort should prevent aborting the readable');

promise_test(t => {
  const rs = recordingReadableStream(errorOnPull, hwm0);
  const ws = recordingWritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal, preventCancel: true, preventAbort: true }),
                         'pipeTo should reject')
    .then(() => {
      assert_equals(rs.events.length, 0, 'cancel should not be called');
      assert_equals(ws.events.length, 0, 'writable should not have been aborted');
      return ws.getWriter().ready;
    });
}, 'preventCancel and preventAbort should prevent canceling the readable and aborting the readable');

for (const reason of [null, undefined, error1]) {
  promise_test(async t => {
    const rs = new ReadableStream({
      start(controller) {
        controller.enqueue('a');
        controller.enqueue('b');
        controller.close();
      }
    });
    const abortController = new AbortController();
    const signal = abortController.signal;
    const ws = recordingWritableStream({
      write() {
        abortController.abort(reason);
      }
    });
    const pipeToPromise = rs.pipeTo(ws, { signal });
    if (reason !== undefined) {
      await promise_rejects_exactly(t, reason, pipeToPromise, 'pipeTo rejects with abort reason');
    } else {
      await promise_rejects_dom(t, 'AbortError', pipeToPromise, 'pipeTo rejects with AbortError');
    }
    const error = await pipeToPromise.catch(e => e);
    assert_equals(signal.reason, error, 'signal.reason should be error');
    assert_equals(ws.events.length, 4, 'only chunk "a" should have been written');
    assert_array_equals(ws.events.slice(0, 3), ['write', 'a', 'abort'], 'events should match');
    assert_equals(ws.events[3], error, 'abort reason should be error');
  }, `(reason: '${reason}') abort should prevent further reads`);
}

for (const reason of [null, undefined, error1]) {
  promise_test(async t => {
    let readController;
    const rs = new ReadableStream({
      start(c) {
        readController = c;
        c.enqueue('a');
        c.enqueue('b');
      }
    });
    const abortController = new AbortController();
    const signal = abortController.signal;
    let resolveWrite;
    const writePromise = new Promise(resolve => {
      resolveWrite = resolve;
    });
    const ws = recordingWritableStream({
      write() {
        return writePromise;
      }
    }, new CountQueuingStrategy({ highWaterMark: Infinity }));
    const pipeToPromise = rs.pipeTo(ws, { signal });
    await delay(0);
    await abortController.abort(reason);
    await readController.close(); // Make sure the test terminates when signal is not implemented.
    await resolveWrite();
    if (reason !== undefined) {
      await promise_rejects_exactly(t, reason, pipeToPromise, 'pipeTo rejects with abort reason');
    } else {
      await promise_rejects_dom(t, 'AbortError', pipeToPromise, 'pipeTo rejects with AbortError');
    }
    const error = await pipeToPromise.catch(e => e);
    assert_equals(signal.reason, error, 'signal.reason should be error');
    assert_equals(ws.events.length, 6, 'chunks "a" and "b" should have been written');
    assert_array_equals(ws.events.slice(0, 5), ['write', 'a', 'write', 'b', 'abort'], 'events should match');
    assert_equals(ws.events[5], error, 'abort reason should be error');
  }, `(reason: '${reason}') all pending writes should complete on abort`);
}

promise_test(t => {
  const rs = new ReadableStream({
    pull(controller) {
      controller.error('failed to abort');
    },
    cancel() {
      return Promise.reject(error1);
    }
  }, hwm0);
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { signal }), 'pipeTo should reject');
}, 'a rejection from underlyingSource.cancel() should be returned by pipeTo()');

promise_test(t => {
  const rs = new ReadableStream(errorOnPull, hwm0);
  const ws = new WritableStream({
    abort() {
      return Promise.reject(error1);
    }
  });
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_exactly(t, error1, rs.pipeTo(ws, { signal }), 'pipeTo should reject');
}, 'a rejection from underlyingSink.abort() should be returned by pipeTo()');

promise_test(t => {
  const events = [];
  const rs = new ReadableStream({
    pull(controller) {
      controller.error('failed to abort');
    },
    cancel() {
      events.push('cancel');
      return Promise.reject(error1);
    }
  }, hwm0);
  const ws = new WritableStream({
    abort() {
      events.push('abort');
      return Promise.reject(error2);
    }
  });
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_exactly(t, error2, rs.pipeTo(ws, { signal }), 'pipeTo should reject')
      .then(() => assert_array_equals(events, ['abort', 'cancel'], 'abort() should be called before cancel()'));
}, 'a rejection from underlyingSink.abort() should be preferred to one from underlyingSource.cancel()');

promise_test(t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal }), 'pipeTo should reject');
}, 'abort signal takes priority over closed readable');

promise_test(t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal }), 'pipeTo should reject');
}, 'abort signal takes priority over errored readable');

promise_test(t => {
  const rs = new ReadableStream({
    pull(controller) {
      controller.error('failed to abort');
    }
  }, hwm0);
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  const writer = ws.getWriter();
  return writer.close().then(() => {
    writer.releaseLock();
    return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal }), 'pipeTo should reject');
  });
}, 'abort signal takes priority over closed writable');

promise_test(t => {
  const rs = new ReadableStream({
    pull(controller) {
      controller.error('failed to abort');
    }
  }, hwm0);
  const ws = new WritableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  const abortController = new AbortController();
  const signal = abortController.signal;
  abortController.abort();
  return promise_rejects_dom(t, 'AbortError', rs.pipeTo(ws, { signal }), 'pipeTo should reject');
}, 'abort signal takes priority over errored writable');

promise_test(() => {
  let readController;
  const rs = new ReadableStream({
    start(c) {
      readController = c;
    }
  });
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  const pipeToPromise = rs.pipeTo(ws, { signal, preventClose: true });
  readController.close();
  return Promise.resolve().then(() => {
    abortController.abort();
    return pipeToPromise;
  }).then(() => ws.getWriter().write('this should succeed'));
}, 'abort should do nothing after the readable is closed');

promise_test(t => {
  let readController;
  const rs = new ReadableStream({
    start(c) {
      readController = c;
    }
  });
  const ws = new WritableStream();
  const abortController = new AbortController();
  const signal = abortController.signal;
  const pipeToPromise = rs.pipeTo(ws, { signal, preventAbort: true });
  readController.error(error1);
  return Promise.resolve().then(() => {
    abortController.abort();
    return promise_rejects_exactly(t, error1, pipeToPromise, 'pipeTo should reject');
  }).then(() => ws.getWriter().write('this should succeed'));
}, 'abort should do nothing after the readable is errored');

promise_test(t => {
  let readController;
  const rs = new ReadableStream({
    start(c) {
      readController = c;
    }
  });
  let resolveWrite;
  const writePromise = new Promise(resolve => {
    resolveWrite = resolve;
  });
  const ws = new WritableStream({
    write() {
      readController.error(error1);
      return writePromise;
    }
  });
  const abortController = new AbortController();
  const signal = abortController.signal;
  const pipeToPromise = rs.pipeTo(ws, { signal, preventAbort: true });
  readController.enqueue('a');
  return delay(0).then(() => {
    abortController.abort();
    resolveWrite();
    return promise_rejects_exactly(t, error1, pipeToPromise, 'pipeTo should reject');
  }).then(() => ws.getWriter().write('this should succeed'));
}, 'abort should do nothing after the readable is errored, even with pending writes');

promise_test(t => {
  const rs = recordingReadableStream({
    pull(controller) {
      return delay(0).then(() => controller.close());
    }
  });
  let writeController;
  const ws = new WritableStream({
    start(c) {
      writeController = c;
    }
  });
  const abortController = new AbortController();
  const signal = abortController.signal;
  const pipeToPromise = rs.pipeTo(ws, { signal, preventCancel: true });
  return Promise.resolve().then(() => {
    writeController.error(error1);
    return Promise.resolve();
  }).then(() => {
    abortController.abort();
    return promise_rejects_exactly(t, error1, pipeToPromise, 'pipeTo should reject');
  }).then(() => {
    assert_array_equals(rs.events, ['pull'], 'cancel should not have been called');
  });
}, 'abort should do nothing after the writable is errored');

promise_test(async t => {
  const rs = new ReadableStream({
    pull(c) {
      c.enqueue(new Uint8Array([]));
    },
    type: "bytes",
  });
  const ws = new WritableStream();
  const [first, second] = rs.tee();

  let aborted = false;
  first.pipeTo(ws, { signal: AbortSignal.abort() }).catch(() => {
    aborted = true;
  });
  await delay(0);
  assert_true(!aborted, "pipeTo should not resolve yet");
  await second.cancel();
  await delay(0);
  assert_true(aborted, "pipeTo should be aborted now");
}, "pipeTo on a teed readable byte stream should only be aborted when both branches are aborted");

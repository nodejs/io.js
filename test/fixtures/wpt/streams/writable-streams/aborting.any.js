// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

promise_test(t => {
  const ws = new WritableStream({
    write: t.unreached_func('write() should not be called')
  });

  const writer = ws.getWriter();
  const writePromise = writer.write('a');

  const readyPromise = writer.ready;

  writer.abort(error1);

  assert_equals(writer.ready, readyPromise, 'the ready promise property should not change');

  return Promise.all([
    promise_rejects_exactly(t, error1, readyPromise, 'the ready promise should reject with error1'),
    promise_rejects_exactly(t, error1, writePromise, 'the write() promise should reject with error1')
  ]);
}, 'Aborting a WritableStream before it starts should cause the writer\'s unsettled ready promise to reject');

promise_test(t => {
  const ws = new WritableStream();

  const writer = ws.getWriter();
  writer.write('a');

  const readyPromise = writer.ready;

  return readyPromise.then(() => {
    writer.abort(error1);

    assert_not_equals(writer.ready, readyPromise, 'the ready promise property should change');
    return promise_rejects_exactly(t, error1, writer.ready, 'the ready promise should reject with error1');
  });
}, 'Aborting a WritableStream should cause the writer\'s fulfilled ready promise to reset to a rejected one');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();

  writer.releaseLock();

  return promise_rejects_js(t, TypeError, writer.abort(), 'abort() should reject with a TypeError');
}, 'abort() on a released writer rejects');

promise_test(t => {
  const ws = recordingWritableStream();

  return delay(0)
    .then(() => {
      const writer = ws.getWriter();

      const abortPromise = writer.abort(error1);

      return Promise.all([
        promise_rejects_exactly(t, error1, writer.write(1), 'write(1) must reject with error1'),
        promise_rejects_exactly(t, error1, writer.write(2), 'write(2) must reject with error1'),
        abortPromise
      ]);
    })
    .then(() => {
      assert_array_equals(ws.events, ['abort', error1]);
    });
}, 'Aborting a WritableStream immediately prevents future writes');

promise_test(t => {
  const ws = recordingWritableStream();
  const results = [];

  return delay(0)
    .then(() => {
      const writer = ws.getWriter();

      results.push(
        writer.write(1),
        promise_rejects_exactly(t, error1, writer.write(2), 'write(2) must reject with error1'),
        promise_rejects_exactly(t, error1, writer.write(3), 'write(3) must reject with error1')
      );

      const abortPromise = writer.abort(error1);

      results.push(
        promise_rejects_exactly(t, error1, writer.write(4), 'write(4) must reject with error1'),
        promise_rejects_exactly(t, error1, writer.write(5), 'write(5) must reject with error1')
      );

      return abortPromise;
    }).then(() => {
      assert_array_equals(ws.events, ['write', 1, 'abort', error1]);

      return Promise.all(results);
    });
}, 'Aborting a WritableStream prevents further writes after any that are in progress');

promise_test(() => {
  const ws = new WritableStream({
    abort() {
      return 'Hello';
    }
  });
  const writer = ws.getWriter();

  return writer.abort('a').then(value => {
    assert_equals(value, undefined, 'fulfillment value must be undefined');
  });
}, 'Fulfillment value of writer.abort() call must be undefined even if the underlying sink returns a non-undefined ' +
   'value');

promise_test(t => {
  const ws = new WritableStream({
    abort() {
      throw error1;
    }
  });
  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.abort(undefined),
    'rejection reason of abortPromise must be the error thrown by abort');
}, 'WritableStream if sink\'s abort throws, the promise returned by writer.abort() rejects');

promise_test(t => {
  const ws = new WritableStream({
    abort() {
      throw error1;
    }
  });
  const writer = ws.getWriter();

  const abortPromise1 = writer.abort(undefined);
  const abortPromise2 = writer.abort(undefined);

  assert_equals(abortPromise1, abortPromise2, 'the promises must be the same');

  return promise_rejects_exactly(t, error1, abortPromise1, 'promise must have matching rejection');
}, 'WritableStream if sink\'s abort throws, the promise returned by multiple writer.abort()s is the same and rejects');

promise_test(t => {
  const ws = new WritableStream({
    abort() {
      throw error1;
    }
  });

  return promise_rejects_exactly(t, error1, ws.abort(undefined),
    'rejection reason of abortPromise must be the error thrown by abort');
}, 'WritableStream if sink\'s abort throws, the promise returned by ws.abort() rejects');

promise_test(t => {
  let resolveWritePromise;
  const ws = new WritableStream({
    write() {
      return new Promise(resolve => {
        resolveWritePromise = resolve;
      });
    },
    abort() {
      throw error1;
    }
  });

  const writer = ws.getWriter();

  writer.write().catch(() => {});
  return flushAsyncEvents().then(() => {
    const abortPromise = writer.abort(undefined);

    resolveWritePromise();
    return promise_rejects_exactly(t, error1, abortPromise,
      'rejection reason of abortPromise must be the error thrown by abort');
  });
}, 'WritableStream if sink\'s abort throws, for an abort performed during a write, the promise returned by ' +
   'ws.abort() rejects');

promise_test(() => {
  const ws = recordingWritableStream();
  const writer = ws.getWriter();

  return writer.abort(error1).then(() => {
    assert_array_equals(ws.events, ['abort', error1]);
  });
}, 'Aborting a WritableStream passes through the given reason');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();

  const abortPromise = writer.abort(error1);

  const events = [];
  writer.ready.catch(() => {
    events.push('ready');
  });
  writer.closed.catch(() => {
    events.push('closed');
  });

  return Promise.all([
    abortPromise,
    promise_rejects_exactly(t, error1, writer.write(), 'writing should reject with error1'),
    promise_rejects_exactly(t, error1, writer.close(), 'closing should reject with error1'),
    promise_rejects_exactly(t, error1, writer.ready, 'ready should reject with error1'),
    promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with error1')
  ]).then(() => {
    assert_array_equals(['ready', 'closed'], events, 'ready should reject before closed');
  });
}, 'Aborting a WritableStream puts it in an errored state with the error passed to abort()');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();

  const writePromise = promise_rejects_exactly(t, error1, writer.write('a'),
    'writing should reject with error1');

  writer.abort(error1);

  return writePromise;
}, 'Aborting a WritableStream causes any outstanding write() promises to be rejected with the reason supplied');

promise_test(t => {
  const ws = recordingWritableStream();
  const writer = ws.getWriter();

  const closePromise = writer.close();
  const abortPromise = writer.abort(error1);

  return Promise.all([
    promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with error1'),
    promise_rejects_exactly(t, error1, closePromise, 'close() should reject with error1'),
    abortPromise
  ]).then(() => {
    assert_array_equals(ws.events, ['abort', error1]);
  });
}, 'Closing but then immediately aborting a WritableStream causes the stream to error');

promise_test(() => {
  let resolveClose;
  const ws = new WritableStream({
    close() {
      return new Promise(resolve => {
        resolveClose = resolve;
      });
    }
  });
  const writer = ws.getWriter();

  const closePromise = writer.close();

  return delay(0).then(() => {
    const abortPromise = writer.abort(error1);
    resolveClose();
    return Promise.all([
      writer.closed,
      abortPromise,
      closePromise
    ]);
  });
}, 'Closing a WritableStream and aborting it while it closes causes the stream to ignore the abort attempt');

promise_test(() => {
  const ws = new WritableStream();
  const writer = ws.getWriter();

  writer.close();

  return delay(0).then(() => writer.abort());
}, 'Aborting a WritableStream after it is closed is a no-op');

promise_test(t => {
  // Testing that per https://github.com/whatwg/streams/issues/620#issuecomment-263483953 the fallback to close was
  // removed.

  // Cannot use recordingWritableStream since it always has an abort
  let closeCalled = false;
  const ws = new WritableStream({
    close() {
      closeCalled = true;
    }
  });

  const writer = ws.getWriter();

  writer.abort(error1);

  return promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with error1').then(() => {
    assert_false(closeCalled, 'close must not have been called');
  });
}, 'WritableStream should NOT call underlying sink\'s close if no abort is supplied (historical)');

promise_test(() => {
  let thenCalled = false;
  const ws = new WritableStream({
    abort() {
      return {
        then(onFulfilled) {
          thenCalled = true;
          onFulfilled();
        }
      };
    }
  });
  const writer = ws.getWriter();
  return writer.abort().then(() => assert_true(thenCalled, 'then() should be called'));
}, 'returning a thenable from abort() should work');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return flushAsyncEvents();
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    writer.abort(error1);
    let closedRejected = false;
    return Promise.all([
      writePromise.then(() => assert_false(closedRejected, '.closed should not resolve before write()')),
      promise_rejects_exactly(t, error1, writer.closed, '.closed should reject').then(() => {
        closedRejected = true;
      })
    ]);
  });
}, '.closed should not resolve before fulfilled write()');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return Promise.reject(error1);
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    const abortPromise = writer.abort(error2);
    let closedRejected = false;
    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise, 'write() should reject')
          .then(() => assert_false(closedRejected, '.closed should not resolve before write()')),
      promise_rejects_exactly(t, error2, writer.closed, '.closed should reject')
          .then(() => {
            closedRejected = true;
          }),
      abortPromise
    ]);
  });
}, '.closed should not resolve before rejected write(); write() error should not overwrite abort() error');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return flushAsyncEvents();
    }
  }, new CountQueuingStrategy({ highWaterMark: 4 }));
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const settlementOrder = [];
    return Promise.all([
      writer.write('1').then(() => settlementOrder.push(1)),
      promise_rejects_exactly(t, error1, writer.write('2'), 'first queued write should be rejected')
          .then(() => settlementOrder.push(2)),
      promise_rejects_exactly(t, error1, writer.write('3'), 'second queued write should be rejected')
          .then(() => settlementOrder.push(3)),
      writer.abort(error1)
    ]).then(() => assert_array_equals([1, 2, 3], settlementOrder, 'writes should be satisfied in order'));
  });
}, 'writes should be satisfied in order when aborting');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return Promise.reject(error1);
    }
  }, new CountQueuingStrategy({ highWaterMark: 4 }));
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const settlementOrder = [];
    return Promise.all([
      promise_rejects_exactly(t, error1, writer.write('1'), 'in-flight write should be rejected')
          .then(() => settlementOrder.push(1)),
      promise_rejects_exactly(t, error2, writer.write('2'), 'first queued write should be rejected')
          .then(() => settlementOrder.push(2)),
      promise_rejects_exactly(t, error2, writer.write('3'), 'second queued write should be rejected')
          .then(() => settlementOrder.push(3)),
      writer.abort(error2)
    ]).then(() => assert_array_equals([1, 2, 3], settlementOrder, 'writes should be satisfied in order'));
  });
}, 'writes should be satisfied in order after rejected write when aborting');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return Promise.reject(error1);
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    return Promise.all([
      promise_rejects_exactly(t, error1, writer.write('a'), 'writer.write() should reject with error from underlying write()'),
      promise_rejects_exactly(t, error2, writer.close(),
                              'writer.close() should reject with error from underlying write()'),
      writer.abort(error2)
    ]);
  });
}, 'close() should reject with abort reason why abort() is first error');

promise_test(() => {
  let resolveWrite;
  const ws = recordingWritableStream({
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.write('a');
    const abortPromise = writer.abort('b');
    return flushAsyncEvents().then(() => {
      assert_array_equals(ws.events, ['write', 'a'], 'abort should not be called while write is in-flight');
      resolveWrite();
      return abortPromise.then(() => {
        assert_array_equals(ws.events, ['write', 'a', 'abort', 'b'], 'abort should be called after the write finishes');
      });
    });
  });
}, 'underlying abort() should not be called until underlying write() completes');

promise_test(() => {
  let resolveClose;
  const ws = recordingWritableStream({
    close() {
      return new Promise(resolve => {
        resolveClose = resolve;
      });
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.close();
    const abortPromise = writer.abort();
    return flushAsyncEvents().then(() => {
      assert_array_equals(ws.events, ['close'], 'abort should not be called while close is in-flight');
      resolveClose();
      return abortPromise.then(() => {
        assert_array_equals(ws.events, ['close'], 'abort should not be called');
      });
    });
  });
}, 'underlying abort() should not be called if underlying close() has started');

promise_test(t => {
  let rejectClose;
  let abortCalled = false;
  const ws = new WritableStream({
    close() {
      return new Promise((resolve, reject) => {
        rejectClose = reject;
      });
    },
    abort() {
      abortCalled = true;
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const closePromise = writer.close();
    const abortPromise = writer.abort();
    return flushAsyncEvents().then(() => {
      assert_false(abortCalled, 'underlying abort should not be called while close is in-flight');
      rejectClose(error1);
      return promise_rejects_exactly(t, error1, abortPromise, 'abort should reject with the same reason').then(() => {
        return promise_rejects_exactly(t, error1, closePromise, 'close should reject with the same reason');
      }).then(() => {
        assert_false(abortCalled, 'underlying abort should not be called after close completes');
      });
    });
  });
}, 'if underlying close() has started and then rejects, the abort() and close() promises should reject with the ' +
   'underlying close rejection reason');

promise_test(t => {
  let resolveWrite;
  const ws = recordingWritableStream({
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.write('a');
    const closePromise = writer.close();
    const abortPromise = writer.abort(error1);

    return flushAsyncEvents().then(() => {
      assert_array_equals(ws.events, ['write', 'a'], 'abort should not be called while write is in-flight');
      resolveWrite();
      return abortPromise.then(() => {
        assert_array_equals(ws.events, ['write', 'a', 'abort', error1], 'abort should be called after write completes');
        return promise_rejects_exactly(t, error1, closePromise, 'promise returned by close() should be rejected');
      });
    });
  });
}, 'an abort() that happens during a write() should trigger the underlying abort() even with a close() queued');

promise_test(t => {
  const ws = new WritableStream({
    write() {
      return new Promise(() => {});
    }
  });

  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.write('a');
    writer.abort(error1);
    writer.releaseLock();
    const writer2 = ws.getWriter();
    return promise_rejects_exactly(t, error1, writer2.ready,
                                   'ready of the second writer should be rejected with error1');
  });
}, 'if a writer is created for a stream with a pending abort, its ready should be rejected with the abort error');

promise_test(() => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const closePromise = writer.close();
    const abortPromise = writer.abort();
    const events = [];
    return Promise.all([
      closePromise.then(() => { events.push('close'); }),
      abortPromise.then(() => { events.push('abort'); })
    ]).then(() => {
      assert_array_equals(events, ['close', 'abort']);
    });
  });
}, 'writer close() promise should resolve before abort() promise');

promise_test(t => {
  const ws = new WritableStream({
    write(chunk, controller) {
      controller.error(error1);
      return new Promise(() => {});
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    writer.write('a');
    return promise_rejects_exactly(t, error1, writer.ready, 'writer.ready should reject');
  });
}, 'writer.ready should reject on controller error without waiting for underlying write');

promise_test(t => {
  let rejectWrite;
  const ws = new WritableStream({
    write() {
      return new Promise((resolve, reject) => {
        rejectWrite = reject;
      });
    }
  });

  let writePromise;
  let abortPromise;

  const events = [];

  const writer = ws.getWriter();

  writer.closed.catch(() => {
    events.push('closed');
  });

  // Wait for ws to start
  return flushAsyncEvents().then(() => {
    writePromise = writer.write('a');
    writePromise.catch(() => {
      events.push('writePromise');
    });

    abortPromise = writer.abort(error1);
    abortPromise.then(() => {
      events.push('abortPromise');
    });

    const writePromise2 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise2, 'writePromise2 must reject with the error from abort'),
      promise_rejects_exactly(t, error1, writer.ready, 'writer.ready must reject with the error from abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, [], 'writePromise, abortPromise and writer.closed must not be rejected yet');

    rejectWrite(error2);

    return Promise.all([
      promise_rejects_exactly(t, error2, writePromise,
                              'writePromise must reject with the error returned from the sink\'s write method'),
      abortPromise,
      promise_rejects_exactly(t, error1, writer.closed,
                              'writer.closed must reject with the error from abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, ['writePromise', 'abortPromise', 'closed'],
                        'writePromise, abortPromise and writer.closed must settle');

    const writePromise3 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise3,
                              'writePromise3 must reject with the error from abort'),
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must be still rejected with the error indicating abort')
    ]);
  }).then(() => {
    writer.releaseLock();

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.ready,
                      'writer.ready must be rejected with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.closed,
                      'writer.closed must be rejected with an error indicating release')
    ]);
  });
}, 'writer.abort() while there is an in-flight write, and then finish the write with rejection');

promise_test(t => {
  let resolveWrite;
  let controller;
  const ws = new WritableStream({
    write(chunk, c) {
      controller = c;
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });

  let writePromise;
  let abortPromise;

  const events = [];

  const writer = ws.getWriter();

  writer.closed.catch(() => {
    events.push('closed');
  });

  // Wait for ws to start
  return flushAsyncEvents().then(() => {
    writePromise = writer.write('a');
    writePromise.then(() => {
      events.push('writePromise');
    });

    abortPromise = writer.abort(error1);
    abortPromise.then(() => {
      events.push('abortPromise');
    });

    const writePromise2 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise2, 'writePromise2 must reject with the error from abort'),
      promise_rejects_exactly(t, error1, writer.ready, 'writer.ready must reject with the error from abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, [], 'writePromise, abortPromise and writer.closed must not be fulfilled/rejected yet');

    // This error is too late to change anything. abort() has already changed the stream state to 'erroring'.
    controller.error(error2);

    const writePromise3 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise3,
                              'writePromise3 must reject with the error from abort'),
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must be still rejected with the error indicating abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(
        events, [],
        'writePromise, abortPromise and writer.closed must not be fulfilled/rejected yet even after ' +
            'controller.error() call');

    resolveWrite();

    return Promise.all([
      writePromise,
      abortPromise,
      promise_rejects_exactly(t, error1, writer.closed,
                              'writer.closed must reject with the error from abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, ['writePromise', 'abortPromise', 'closed'],
                        'writePromise, abortPromise and writer.closed must settle');

    const writePromise4 = writer.write('a');

    return Promise.all([
      writePromise,
      promise_rejects_exactly(t, error1, writePromise4,
                              'writePromise4 must reject with the error from abort'),
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must be still rejected with the error indicating abort')
    ]);
  }).then(() => {
    writer.releaseLock();

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.ready,
                      'writer.ready must be rejected with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.closed,
                      'writer.closed must be rejected with an error indicating release')
    ]);
  });
}, 'writer.abort(), controller.error() while there is an in-flight write, and then finish the write');

promise_test(t => {
  let resolveClose;
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      return new Promise(resolve => {
        resolveClose = resolve;
      });
    }
  });

  let closePromise;
  let abortPromise;

  const events = [];

  const writer = ws.getWriter();

  writer.closed.then(() => {
    events.push('closed');
  });

  // Wait for ws to start
  return flushAsyncEvents().then(() => {
    closePromise = writer.close();
    closePromise.then(() => {
      events.push('closePromise');
    });

    abortPromise = writer.abort(error1);
    abortPromise.then(() => {
      events.push('abortPromise');
    });

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.close(),
        'writer.close() must reject with an error indicating already closing'),
      promise_rejects_exactly(t, error1, writer.ready, 'writer.ready must reject with the error from abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, [], 'closePromise, abortPromise and writer.closed must not be fulfilled/rejected yet');

    controller.error(error2);

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.close(),
        'writer.close() must reject with an error indicating already closing'),
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must be still rejected with the error indicating abort'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(
        events, [],
        'closePromise, abortPromise and writer.closed must not be fulfilled/rejected yet even after ' +
            'controller.error() call');

    resolveClose();

    return Promise.all([
      closePromise,
      abortPromise,
      writer.closed,
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, ['closePromise', 'abortPromise', 'closed'],
                        'closedPromise, abortPromise and writer.closed must fulfill');

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.close(),
        'writer.close() must reject with an error indicating already closing'),
      promise_rejects_exactly(t, error1, writer.ready,
                              'writer.ready must be still rejected with the error indicating abort')
    ]);
  }).then(() => {
    writer.releaseLock();

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.close(),
        'writer.close() must reject with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.ready,
                      'writer.ready must be rejected with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.closed,
                      'writer.closed must be rejected with an error indicating release')
    ]);
  });
}, 'writer.abort(), controller.error() while there is an in-flight close, and then finish the close');

promise_test(t => {
  let resolveWrite;
  let controller;
  const ws = recordingWritableStream({
    write(chunk, c) {
      controller = c;
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });

  let writePromise;
  let abortPromise;

  const events = [];

  const writer = ws.getWriter();

  writer.closed.catch(() => {
    events.push('closed');
  });

  // Wait for ws to start
  return flushAsyncEvents().then(() => {
    writePromise = writer.write('a');
    writePromise.then(() => {
      events.push('writePromise');
    });

    controller.error(error2);

    const writePromise2 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error2, writePromise2,
                              'writePromise2 must reject with the error passed to the controller\'s error method'),
      promise_rejects_exactly(t, error2, writer.ready,
                              'writer.ready must reject with the error passed to the controller\'s error method'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, [], 'writePromise and writer.closed must not be fulfilled/rejected yet');

    abortPromise = writer.abort(error1);
    abortPromise.catch(() => {
      events.push('abortPromise');
    });

    const writePromise3 = writer.write('a');

    return Promise.all([
      promise_rejects_exactly(t, error2, writePromise3,
                              'writePromise3 must reject with the error passed to the controller\'s error method'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(
        events, [],
        'writePromise and writer.closed must not be fulfilled/rejected yet even after writer.abort()');

    resolveWrite();

    return Promise.all([
      promise_rejects_exactly(t, error2, abortPromise,
                              'abort() must reject with the error passed to the controller\'s error method'),
      promise_rejects_exactly(t, error2, writer.closed,
                              'writer.closed must reject with the error passed to the controller\'s error method'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, ['writePromise', 'abortPromise', 'closed'],
                        'writePromise, abortPromise and writer.closed must fulfill/reject');
    assert_array_equals(ws.events, ['write', 'a'], 'sink abort() should not be called');

    const writePromise4 = writer.write('a');

    return Promise.all([
      writePromise,
      promise_rejects_exactly(t, error2, writePromise4,
                              'writePromise4 must reject with the error passed to the controller\'s error method'),
      promise_rejects_exactly(t, error2, writer.ready,
                              'writer.ready must be still rejected with the error passed to the controller\'s error method')
    ]);
  }).then(() => {
    writer.releaseLock();

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.ready,
                      'writer.ready must be rejected with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.closed,
                      'writer.closed must be rejected with an error indicating release')
    ]);
  });
}, 'controller.error(), writer.abort() while there is an in-flight write, and then finish the write');

promise_test(t => {
  let resolveClose;
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      return new Promise(resolve => {
        resolveClose = resolve;
      });
    }
  });

  let closePromise;
  let abortPromise;

  const events = [];

  const writer = ws.getWriter();

  writer.closed.then(() => {
    events.push('closed');
  });

  // Wait for ws to start
  return flushAsyncEvents().then(() => {
    closePromise = writer.close();
    closePromise.then(() => {
      events.push('closePromise');
    });

    controller.error(error2);

    return flushAsyncEvents();
  }).then(() => {
    assert_array_equals(events, [], 'closePromise must not be fulfilled/rejected yet');

    abortPromise = writer.abort(error1);
    abortPromise.then(() => {
      events.push('abortPromise');
    });

    return Promise.all([
      promise_rejects_exactly(t, error2, writer.ready,
                              'writer.ready must reject with the error passed to the controller\'s error method'),
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(
        events, [],
        'closePromise and writer.closed must not be fulfilled/rejected yet even after writer.abort()');

    resolveClose();

    return Promise.all([
      closePromise,
      promise_rejects_exactly(t, error2, writer.ready,
                              'writer.ready must be still rejected with the error passed to the controller\'s error method'),
      writer.closed,
      flushAsyncEvents()
    ]);
  }).then(() => {
    assert_array_equals(events, ['closePromise', 'abortPromise', 'closed'],
                        'abortPromise, closePromise and writer.closed must fulfill/reject');
  }).then(() => {
    writer.releaseLock();

    return Promise.all([
      promise_rejects_js(t, TypeError, writer.ready,
                      'writer.ready must be rejected with an error indicating release'),
      promise_rejects_js(t, TypeError, writer.closed,
                      'writer.closed must be rejected with an error indicating release')
    ]);
  });
}, 'controller.error(), writer.abort() while there is an in-flight close, and then finish the close');

promise_test(t => {
  let resolveWrite;
  const ws = new WritableStream({
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    const closed = writer.closed;
    const abortPromise = writer.abort();
    writer.releaseLock();
    resolveWrite();
    return Promise.all([
      writePromise,
      abortPromise,
      promise_rejects_js(t, TypeError, closed, 'closed should reject')]);
  });
}, 'releaseLock() while aborting should reject the original closed promise');

// TODO(ricea): Consider removing this test if it is no longer useful.
promise_test(t => {
  let resolveWrite;
  let resolveAbort;
  let resolveAbortStarted;
  const abortStarted = new Promise(resolve => {
    resolveAbortStarted = resolve;
  });
  const ws = new WritableStream({
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    },
    abort() {
      resolveAbortStarted();
      return new Promise(resolve => {
        resolveAbort = resolve;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    const closed = writer.closed;
    const abortPromise = writer.abort();
    resolveWrite();
    return abortStarted.then(() => {
      writer.releaseLock();
      assert_equals(writer.closed, closed, 'closed promise should not have changed');
      resolveAbort();
      return Promise.all([
        writePromise,
        abortPromise,
        promise_rejects_js(t, TypeError, closed, 'closed should reject')]);
    });
  });
}, 'releaseLock() during delayed async abort() should reject the writer.closed promise');

promise_test(() => {
  let resolveStart;
  const ws = recordingWritableStream({
    start() {
      return new Promise(resolve => {
        resolveStart = resolve;
      });
    }
  });
  const abortPromise = ws.abort('done');
  return flushAsyncEvents().then(() => {
    assert_array_equals(ws.events, [], 'abort() should not be called during start()');
    resolveStart();
    return abortPromise.then(() => {
      assert_array_equals(ws.events, ['abort', 'done'], 'abort() should be called after start() is done');
    });
  });
}, 'sink abort() should not be called until sink start() is done');

promise_test(() => {
  let resolveStart;
  let controller;
  const ws = recordingWritableStream({
    start(c) {
      controller = c;
      return new Promise(resolve => {
        resolveStart = resolve;
      });
    }
  });
  const abortPromise = ws.abort('done');
  controller.error(error1);
  resolveStart();
  return abortPromise.then(() =>
      assert_array_equals(ws.events, ['abort', 'done'],
                          'abort() should still be called if start() errors the controller'));
}, 'if start attempts to error the controller after abort() has been called, then it should lose');

promise_test(() => {
  const ws = recordingWritableStream({
    start() {
      return Promise.reject(error1);
    }
  });
  return ws.abort('done').then(() =>
      assert_array_equals(ws.events, ['abort', 'done'], 'abort() should still be called if start() rejects'));
}, 'stream abort() promise should still resolve if sink start() rejects');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  const writerReady1 = writer.ready;
  writer.abort(error1);
  const writerReady2 = writer.ready;
  assert_not_equals(writerReady1, writerReady2, 'abort() should replace the ready promise with a rejected one');
  return Promise.all([writerReady1,
                      promise_rejects_exactly(t, error1, writerReady2, 'writerReady2 should reject')]);
}, 'writer abort() during sink start() should replace the writer.ready promise synchronously');

promise_test(t => {
  const events = [];
  const ws = recordingWritableStream();
  const writer = ws.getWriter();
  const writePromise1 = writer.write(1);
  const abortPromise = writer.abort(error1);
  const writePromise2 = writer.write(2);
  const closePromise = writer.close();
  writePromise1.catch(() => events.push('write1'));
  abortPromise.then(() => events.push('abort'));
  writePromise2.catch(() => events.push('write2'));
  closePromise.catch(() => events.push('close'));
  return Promise.all([
    promise_rejects_exactly(t, error1, writePromise1, 'first write() should reject'),
    abortPromise,
    promise_rejects_exactly(t, error1, writePromise2, 'second write() should reject'),
    promise_rejects_exactly(t, error1, closePromise, 'close() should reject')
  ])
  .then(() => {
    assert_array_equals(events, ['write2', 'write1', 'abort', 'close'],
                        'promises should resolve in the standard order');
    assert_array_equals(ws.events, ['abort', error1], 'underlying sink write() should not be called');
  });
}, 'promises returned from other writer methods should be rejected when writer abort() happens during sink start()');

promise_test(t => {
  let writeReject;
  let controller;
  const ws = new WritableStream({
    write(chunk, c) {
      controller = c;
      return new Promise((resolve, reject) => {
        writeReject = reject;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('a');
    const abortPromise = writer.abort();
    controller.error(error1);
    writeReject(error2);
    return Promise.all([
      promise_rejects_exactly(t, error2, writePromise, 'write() should reject with error2'),
      abortPromise
    ]);
  });
}, 'abort() should succeed despite rejection from write');

promise_test(t => {
  let closeReject;
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    },
    close() {
      return new Promise((resolve, reject) => {
        closeReject = reject;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const closePromise = writer.close();
    const abortPromise = writer.abort();
    controller.error(error1);
    closeReject(error2);
    return Promise.all([
      promise_rejects_exactly(t, error2, closePromise, 'close() should reject with error2'),
      promise_rejects_exactly(t, error2, abortPromise, 'abort() should reject with error2')
    ]);
  });
}, 'abort() should be rejected with the rejection returned from close()');

promise_test(t => {
  let rejectWrite;
  const ws = recordingWritableStream({
    write() {
      return new Promise((resolve, reject) => {
        rejectWrite = reject;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('1');
    const abortPromise = writer.abort(error2);
    rejectWrite(error1);
    return Promise.all([
      promise_rejects_exactly(t, error1, writePromise, 'write should reject'),
      abortPromise,
      promise_rejects_exactly(t, error2, writer.closed, 'closed should reject with error2')
    ]);
  }).then(() => {
    assert_array_equals(ws.events, ['write', '1', 'abort', error2], 'abort sink method should be called');
  });
}, 'a rejecting sink.write() should not prevent sink.abort() from being called');

promise_test(() => {
  const ws = recordingWritableStream({
    start() {
      return Promise.reject(error1);
    }
  });
  return ws.abort(error2)
      .then(() => {
        assert_array_equals(ws.events, ['abort', error2]);
      });
}, 'when start errors after stream abort(), underlying sink abort() should be called anyway');

promise_test(() => {
  const ws = new WritableStream();
  const abortPromise1 = ws.abort();
  const abortPromise2 = ws.abort();
  assert_equals(abortPromise1, abortPromise2, 'the promises must be the same');

  return abortPromise1.then(
      v => assert_equals(v, undefined, 'abort() should fulfill with undefined'));
}, 'when calling abort() twice on the same stream, both should give the same promise that fulfills with undefined');

promise_test(() => {
  const ws = new WritableStream();
  const abortPromise1 = ws.abort();

  return abortPromise1.then(v1 => {
    assert_equals(v1, undefined, 'first abort() should fulfill with undefined');

    const abortPromise2 = ws.abort();
    assert_not_equals(abortPromise2, abortPromise1, 'because we waited, the second promise should be a new promise');

    return abortPromise2.then(v2 => {
      assert_equals(v2, undefined, 'second abort() should fulfill with undefined');
    });
  });
}, 'when calling abort() twice on the same stream, but sequentially so so there\'s no pending abort the second time, ' +
   'both should fulfill with undefined');

promise_test(t => {
  const ws = new WritableStream({
    start(c) {
      c.error(error1);
    }
  });

  const writer = ws.getWriter();

  return promise_rejects_exactly(t, error1, writer.closed, 'writer.closed should reject').then(() => {
    return writer.abort().then(
      v => assert_equals(v, undefined, 'abort() should fulfill with undefined'));
  });
}, 'calling abort() on an errored stream should fulfill with undefined');

promise_test(t => {
  let controller;
  let resolveWrite;
  const ws = recordingWritableStream({
    start(c) {
      controller = c;
    },
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise = writer.write('chunk');
    controller.error(error1);
    const abortPromise = writer.abort(error2);
    resolveWrite();
    return Promise.all([
      writePromise,
      promise_rejects_exactly(t, error1, abortPromise, 'abort() should reject')
    ]).then(() => {
      assert_array_equals(ws.events, ['write', 'chunk'], 'sink abort() should not be called');
    });
  });
}, 'sink abort() should not be called if stream was erroring due to controller.error() before abort() was called');

promise_test(t => {
  let resolveWrite;
  let size = 1;
  const ws = recordingWritableStream({
    write() {
      return new Promise(resolve => {
        resolveWrite = resolve;
      });
    }
  }, {
    size() {
      return size;
    },
    highWaterMark: 1
  });
  const writer = ws.getWriter();
  return writer.ready.then(() => {
    const writePromise1 = writer.write('chunk1');
    size = NaN;
    const writePromise2 = writer.write('chunk2');
    const abortPromise = writer.abort(error2);
    resolveWrite();
    return Promise.all([
      writePromise1,
      promise_rejects_js(t, RangeError, writePromise2, 'second write() should reject'),
      promise_rejects_js(t, RangeError, abortPromise, 'abort() should reject')
    ]).then(() => {
      assert_array_equals(ws.events, ['write', 'chunk1'], 'sink abort() should not be called');
    });
  });
}, 'sink abort() should not be called if stream was erroring due to bad strategy before abort() was called');

promise_test(t => {
  const ws = new WritableStream();
  return ws.abort().then(() => {
    const writer = ws.getWriter();
    return writer.closed.then(t.unreached_func('closed promise should not fulfill'),
                              e => assert_equals(e, undefined, 'e should be undefined'));
  });
}, 'abort with no arguments should set the stored error to undefined');

promise_test(t => {
  const ws = new WritableStream();
  return ws.abort(undefined).then(() => {
    const writer = ws.getWriter();
    return writer.closed.then(t.unreached_func('closed promise should not fulfill'),
                              e => assert_equals(e, undefined, 'e should be undefined'));
  });
}, 'abort with an undefined argument should set the stored error to undefined');

promise_test(t => {
  const ws = new WritableStream();
  return ws.abort('string argument').then(() => {
    const writer = ws.getWriter();
    return writer.closed.then(t.unreached_func('closed promise should not fulfill'),
                              e => assert_equals(e, 'string argument', 'e should be \'string argument\''));
  });
}, 'abort with a string argument should set the stored error to that argument');

promise_test(t => {
  const ws = new WritableStream();
  const writer = ws.getWriter();
  return promise_rejects_js(t, TypeError, ws.abort(), 'abort should reject')
    .then(() => writer.ready);
}, 'abort on a locked stream should reject');

test(t => {
  let ctrl;
  const ws = new WritableStream({start(c) { ctrl = c; }});
  const e = Error('hello');

  assert_true(ctrl.signal instanceof AbortSignal);
  assert_false(ctrl.signal.aborted);
  assert_equals(ctrl.signal.reason, undefined, 'signal.reason before abort');
  ws.abort(e);
  assert_true(ctrl.signal.aborted);
  assert_equals(ctrl.signal.reason, e);
}, 'WritableStreamDefaultController.signal');

promise_test(async t => {
  let ctrl;
  let resolve;
  const called = new Promise(r => resolve = r);

  const ws = new WritableStream({
    start(c) { ctrl = c; },
    write() { resolve(); return new Promise(() => {}); }
  });
  const writer = ws.getWriter();

  writer.write(99);
  await called;

  assert_false(ctrl.signal.aborted);
  assert_equals(ctrl.signal.reason, undefined, 'signal.reason before abort');
  writer.abort();
  assert_true(ctrl.signal.aborted);
  assert_true(ctrl.signal.reason instanceof DOMException, 'signal.reason is a DOMException');
  assert_equals(ctrl.signal.reason.name, 'AbortError', 'signal.reason is an AbortError');
}, 'the abort signal is signalled synchronously - write');

promise_test(async t => {
  let ctrl;
  let resolve;
  const called = new Promise(r => resolve = r);

  const ws = new WritableStream({
    start(c) { ctrl = c; },
    close() { resolve(); return new Promise(() => {}); }
  });
  const writer = ws.getWriter();

  writer.close(99);
  await called;

  assert_false(ctrl.signal.aborted);
  writer.abort();
  assert_true(ctrl.signal.aborted);
}, 'the abort signal is signalled synchronously - close');

promise_test(async t => {
  let ctrl;
  const ws = new WritableStream({start(c) { ctrl = c; }});
  const writer = ws.getWriter();

  const e = TypeError();
  ctrl.error(e);
  await promise_rejects_exactly(t, e, writer.closed);
  assert_false(ctrl.signal.aborted);
}, 'the abort signal is not signalled on error');

promise_test(async t => {
  let ctrl;
  const e = TypeError();
  const ws = new WritableStream({
    start(c) { ctrl = c; },
    async write() { throw e; }
  });
  const writer = ws.getWriter();

  await promise_rejects_exactly(t, e, writer.write('hello'), 'write result');
  await promise_rejects_exactly(t, e, writer.closed, 'closed');
  assert_false(ctrl.signal.aborted);
}, 'the abort signal is not signalled on write failure');

promise_test(async t => {
  let ctrl;
  const e = TypeError();
  const ws = new WritableStream({
    start(c) { ctrl = c; },
    async close() { throw e; }
  });
  const writer = ws.getWriter();

  await promise_rejects_exactly(t, e, writer.close(), 'close result');
  await promise_rejects_exactly(t, e, writer.closed, 'closed');
  assert_false(ctrl.signal.aborted);
}, 'the abort signal is not signalled on close failure');

promise_test(async t => {
  let ctrl;
  const e1 = SyntaxError();
  const e2 = TypeError();
  const ws = new WritableStream({
    start(c) { ctrl = c; },
  });

  const writer = ws.getWriter();
  ctrl.signal.addEventListener('abort', () => writer.abort(e2));
  writer.abort(e1);
  assert_true(ctrl.signal.aborted);

  await promise_rejects_exactly(t, e2, writer.closed, 'closed');
}, 'recursive abort() call');

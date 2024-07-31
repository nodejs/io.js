// META: global=window,worker,shadowrealm
// META: script=../resources/rs-utils.js
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

const error1 = new Error('error1');

function assert_iter_result(iterResult, value, done, message) {
  const prefix = message === undefined ? '' : `${message} `;
  assert_equals(typeof iterResult, 'object', `${prefix}type is object`);
  assert_equals(Object.getPrototypeOf(iterResult), Object.prototype, `${prefix}[[Prototype]]`);
  assert_array_equals(Object.getOwnPropertyNames(iterResult).sort(), ['done', 'value'], `${prefix}property names`);
  assert_equals(iterResult.value, value, `${prefix}value`);
  assert_equals(iterResult.done, done, `${prefix}done`);
}

test(() => {
  const s = new ReadableStream();
  const it = s.values();
  const proto = Object.getPrototypeOf(it);

  const AsyncIteratorPrototype = Object.getPrototypeOf(Object.getPrototypeOf(async function* () {}).prototype);
  assert_equals(Object.getPrototypeOf(proto), AsyncIteratorPrototype, 'prototype should extend AsyncIteratorPrototype');

  const methods = ['next', 'return'].sort();
  assert_array_equals(Object.getOwnPropertyNames(proto).sort(), methods, 'should have all the correct methods');

  for (const m of methods) {
    const propDesc = Object.getOwnPropertyDescriptor(proto, m);
    assert_true(propDesc.enumerable, 'method should be enumerable');
    assert_true(propDesc.configurable, 'method should be configurable');
    assert_true(propDesc.writable, 'method should be writable');
    assert_equals(typeof it[m], 'function', 'method should be a function');
    assert_equals(it[m].name, m, 'method should have the correct name');
  }

  assert_equals(it.next.length, 0, 'next should have no parameters');
  assert_equals(it.return.length, 1, 'return should have 1 parameter');
  assert_equals(typeof it.throw, 'undefined', 'throw should not exist');
}, 'Async iterator instances should have the correct list of properties');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    }
  });

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [1, 2, 3]);
}, 'Async-iterating a push source');

promise_test(async () => {
  let i = 1;
  const s = new ReadableStream({
    pull(c) {
      c.enqueue(i);
      if (i >= 3) {
        c.close();
      }
      i += 1;
    }
  });

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [1, 2, 3]);
}, 'Async-iterating a pull source');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(undefined);
      c.enqueue(undefined);
      c.enqueue(undefined);
      c.close();
    }
  });

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [undefined, undefined, undefined]);
}, 'Async-iterating a push source with undefined values');

promise_test(async () => {
  let i = 1;
  const s = new ReadableStream({
    pull(c) {
      c.enqueue(undefined);
      if (i >= 3) {
        c.close();
      }
      i += 1;
    }
  });

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [undefined, undefined, undefined]);
}, 'Async-iterating a pull source with undefined values');

promise_test(async () => {
  let i = 1;
  const s = recordingReadableStream({
    pull(c) {
      c.enqueue(i);
      if (i >= 3) {
        c.close();
      }
      i += 1;
    },
  }, new CountQueuingStrategy({ highWaterMark: 0 }));

  const it = s.values();
  assert_array_equals(s.events, []);

  const read1 = await it.next();
  assert_iter_result(read1, 1, false);
  assert_array_equals(s.events, ['pull']);

  const read2 = await it.next();
  assert_iter_result(read2, 2, false);
  assert_array_equals(s.events, ['pull', 'pull']);

  const read3 = await it.next();
  assert_iter_result(read3, 3, false);
  assert_array_equals(s.events, ['pull', 'pull', 'pull']);

  const read4 = await it.next();
  assert_iter_result(read4, undefined, true);
  assert_array_equals(s.events, ['pull', 'pull', 'pull']);
}, 'Async-iterating a pull source manually');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.error('e');
    },
  });

  try {
    for await (const chunk of s) {}
    assert_unreached();
  } catch (e) {
    assert_equals(e, 'e');
  }
}, 'Async-iterating an errored stream throws');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.close();
    }
  });

  for await (const chunk of s) {
    assert_unreached();
  }
}, 'Async-iterating a closed stream never executes the loop body, but works fine');

promise_test(async () => {
  const s = new ReadableStream();

  const loop = async () => {
    for await (const chunk of s) {
      assert_unreached();
    }
    assert_unreached();
  };

  await Promise.race([
    loop(),
    flushAsyncEvents()
  ]);
}, 'Async-iterating an empty but not closed/errored stream never executes the loop body and stalls the async function');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    },
  });

  const reader = s.getReader();
  const readResult = await reader.read();
  assert_iter_result(readResult, 1, false);
  reader.releaseLock();

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [2, 3]);
}, 'Async-iterating a partially consumed stream');

for (const type of ['throw', 'break', 'return']) {
  for (const preventCancel of [false, true]) {
    promise_test(async () => {
      const s = recordingReadableStream({
        start(c) {
          c.enqueue(0);
        }
      });

      // use a separate function for the loop body so return does not stop the test
      const loop = async () => {
        for await (const c of s.values({ preventCancel })) {
          if (type === 'throw') {
            throw new Error();
          } else if (type === 'break') {
            break;
          } else if (type === 'return') {
            return;
          }
        }
      };

      try {
        await loop();
      } catch (e) {}

      if (preventCancel) {
        assert_array_equals(s.events, ['pull'], `cancel() should not be called`);
      } else {
        assert_array_equals(s.events, ['pull', 'cancel', undefined], `cancel() should be called`);
      }
    }, `Cancellation behavior when ${type}ing inside loop body; preventCancel = ${preventCancel}`);
  }
}

for (const preventCancel of [false, true]) {
  promise_test(async () => {
    const s = recordingReadableStream({
      start(c) {
        c.enqueue(0);
      }
    });

    const it = s.values({ preventCancel });
    await it.return();

    if (preventCancel) {
      assert_array_equals(s.events, [], `cancel() should not be called`);
    } else {
      assert_array_equals(s.events, ['cancel', undefined], `cancel() should be called`);
    }
  }, `Cancellation behavior when manually calling return(); preventCancel = ${preventCancel}`);
}

promise_test(async t => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResult1 = await it.next();
  assert_iter_result(iterResult1, 0, false, '1st next()');

  await promise_rejects_exactly(t, error1, it.next(), '2nd next()');
}, 'next() rejects if the stream errors');

promise_test(async () => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResult = await it.return('return value');
  assert_iter_result(iterResult, 'return value', true);
}, 'return() does not rejects if the stream has not errored yet');

promise_test(async t => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      // Do not error in start() because doing so would prevent acquiring a reader/async iterator.
      c.error(error1);
    }
  });

  const it = s[Symbol.asyncIterator]();

  await flushAsyncEvents();
  await promise_rejects_exactly(t, error1, it.return('return value'));
}, 'return() rejects if the stream has errored');

promise_test(async t => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResult1 = await it.next();
  assert_iter_result(iterResult1, 0, false, '1st next()');

  await promise_rejects_exactly(t, error1, it.next(), '2nd next()');

  const iterResult3 = await it.next();
  assert_iter_result(iterResult3, undefined, true, '3rd next()');
}, 'next() that succeeds; next() that reports an error; next()');

promise_test(async () => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResults = await Promise.allSettled([it.next(), it.next(), it.next()]);

  assert_equals(iterResults[0].status, 'fulfilled', '1st next() promise status');
  assert_iter_result(iterResults[0].value, 0, false, '1st next()');

  assert_equals(iterResults[1].status, 'rejected', '2nd next() promise status');
  assert_equals(iterResults[1].reason, error1, '2nd next() rejection reason');

  assert_equals(iterResults[2].status, 'fulfilled', '3rd next() promise status');
  assert_iter_result(iterResults[2].value, undefined, true, '3rd next()');
}, 'next() that succeeds; next() that reports an error(); next() [no awaiting]');

promise_test(async t => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResult1 = await it.next();
  assert_iter_result(iterResult1, 0, false, '1st next()');

  await promise_rejects_exactly(t, error1, it.next(), '2nd next()');

  const iterResult3 = await it.return('return value');
  assert_iter_result(iterResult3, 'return value', true, 'return()');
}, 'next() that succeeds; next() that reports an error(); return()');

promise_test(async () => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]();

  const iterResults = await Promise.allSettled([it.next(), it.next(), it.return('return value')]);

  assert_equals(iterResults[0].status, 'fulfilled', '1st next() promise status');
  assert_iter_result(iterResults[0].value, 0, false, '1st next()');

  assert_equals(iterResults[1].status, 'rejected', '2nd next() promise status');
  assert_equals(iterResults[1].reason, error1, '2nd next() rejection reason');

  assert_equals(iterResults[2].status, 'fulfilled', 'return() promise status');
  assert_iter_result(iterResults[2].value, 'return value', true, 'return()');
}, 'next() that succeeds; next() that reports an error(); return() [no awaiting]');

promise_test(async () => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      c.enqueue(timesPulled);
      ++timesPulled;
    }
  });
  const it = s[Symbol.asyncIterator]();

  const iterResult1 = await it.next();
  assert_iter_result(iterResult1, 0, false, 'next()');

  const iterResult2 = await it.return('return value');
  assert_iter_result(iterResult2, 'return value', true, 'return()');

  assert_equals(timesPulled, 2);
}, 'next() that succeeds; return()');

promise_test(async () => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      c.enqueue(timesPulled);
      ++timesPulled;
    }
  });
  const it = s[Symbol.asyncIterator]();

  const iterResults = await Promise.allSettled([it.next(), it.return('return value')]);

  assert_equals(iterResults[0].status, 'fulfilled', 'next() promise status');
  assert_iter_result(iterResults[0].value, 0, false, 'next()');

  assert_equals(iterResults[1].status, 'fulfilled', 'return() promise status');
  assert_iter_result(iterResults[1].value, 'return value', true, 'return()');

  assert_equals(timesPulled, 2);
}, 'next() that succeeds; return() [no awaiting]');

promise_test(async () => {
  const rs = new ReadableStream();
  const it = rs.values();

  const iterResult1 = await it.return('return value');
  assert_iter_result(iterResult1, 'return value', true, 'return()');

  const iterResult2 = await it.next();
  assert_iter_result(iterResult2, undefined, true, 'next()');
}, 'return(); next()');

promise_test(async () => {
  const rs = new ReadableStream();
  const it = rs.values();

  const resolveOrder = [];
  const iterResults = await Promise.allSettled([
    it.return('return value').then(result => {
      resolveOrder.push('return');
      return result;
    }),
    it.next().then(result => {
      resolveOrder.push('next');
      return result;
    })
  ]);

  assert_equals(iterResults[0].status, 'fulfilled', 'return() promise status');
  assert_iter_result(iterResults[0].value, 'return value', true, 'return()');

  assert_equals(iterResults[1].status, 'fulfilled', 'next() promise status');
  assert_iter_result(iterResults[1].value, undefined, true, 'next()');

  assert_array_equals(resolveOrder, ['return', 'next'], 'next() resolves after return()');
}, 'return(); next() [no awaiting]');

promise_test(async () => {
  let resolveCancelPromise;
  const rs = recordingReadableStream({
    cancel(reason) {
      return new Promise(r => resolveCancelPromise = r);
    }
  });
  const it = rs.values();

  let returnResolved = false;
  const returnPromise = it.return('return value').then(result => {
    returnResolved = true;
    return result;
  });
  await flushAsyncEvents();
  assert_false(returnResolved, 'return() should not resolve while cancel() promise is pending');

  resolveCancelPromise();
  const iterResult1 = await returnPromise;
  assert_iter_result(iterResult1, 'return value', true, 'return()');

  const iterResult2 = await it.next();
  assert_iter_result(iterResult2, undefined, true, 'next()');
}, 'return(); next() with delayed cancel()');

promise_test(async () => {
  let resolveCancelPromise;
  const rs = recordingReadableStream({
    cancel(reason) {
      return new Promise(r => resolveCancelPromise = r);
    }
  });
  const it = rs.values();

  const resolveOrder = [];
  const returnPromise = it.return('return value').then(result => {
    resolveOrder.push('return');
    return result;
  });
  const nextPromise = it.next().then(result => {
    resolveOrder.push('next');
    return result;
  });

  assert_array_equals(rs.events, ['cancel', 'return value'], 'return() should call cancel()');
  assert_array_equals(resolveOrder, [], 'return() should not resolve before cancel() resolves');

  resolveCancelPromise();
  const iterResult1 = await returnPromise;
  assert_iter_result(iterResult1, 'return value', true, 'return() should resolve with original reason');
  const iterResult2 = await nextPromise;
  assert_iter_result(iterResult2, undefined, true, 'next() should resolve with done result');

  assert_array_equals(rs.events, ['cancel', 'return value'], 'no pull() after cancel()');
  assert_array_equals(resolveOrder, ['return', 'next'], 'next() should resolve after return() resolves');

}, 'return(); next() with delayed cancel() [no awaiting]');

promise_test(async () => {
  const rs = new ReadableStream();
  const it = rs.values();

  const iterResult1 = await it.return('return value 1');
  assert_iter_result(iterResult1, 'return value 1', true, '1st return()');

  const iterResult2 = await it.return('return value 2');
  assert_iter_result(iterResult2, 'return value 2', true, '1st return()');
}, 'return(); return()');

promise_test(async () => {
  const rs = new ReadableStream();
  const it = rs.values();

  const resolveOrder = [];
  const iterResults = await Promise.allSettled([
    it.return('return value 1').then(result => {
      resolveOrder.push('return 1');
      return result;
    }),
    it.return('return value 2').then(result => {
      resolveOrder.push('return 2');
      return result;
    })
  ]);

  assert_equals(iterResults[0].status, 'fulfilled', '1st return() promise status');
  assert_iter_result(iterResults[0].value, 'return value 1', true, '1st return()');

  assert_equals(iterResults[1].status, 'fulfilled', '2nd return() promise status');
  assert_iter_result(iterResults[1].value, 'return value 2', true, '1st return()');

  assert_array_equals(resolveOrder, ['return 1', 'return 2'], '2nd return() resolves after 1st return()');
}, 'return(); return() [no awaiting]');

test(() => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(0);
      c.close();
    },
  });
  s.values();
  assert_throws_js(TypeError, () => s.values(), 'values() should throw');
}, 'values() throws if there\'s already a lock');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    }
  });

  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, [1, 2, 3]);

  const reader = s.getReader();
  await reader.closed;
}, 'Acquiring a reader after exhaustively async-iterating a stream');

promise_test(async t => {
  let timesPulled = 0;
  const s = new ReadableStream({
    pull(c) {
      if (timesPulled === 0) {
        c.enqueue(0);
        ++timesPulled;
      } else {
        c.error(error1);
      }
    }
  });

  const it = s[Symbol.asyncIterator]({ preventCancel: true });

  const iterResult1 = await it.next();
  assert_iter_result(iterResult1, 0, false, '1st next()');

  await promise_rejects_exactly(t, error1, it.next(), '2nd next()');

  const iterResult2 = await it.return('return value');
  assert_iter_result(iterResult2, 'return value', true, 'return()');

  // i.e. it should not reject with a generic "this stream is locked" TypeError.
  const reader = s.getReader();
  await promise_rejects_exactly(t, error1, reader.closed, 'closed on the new reader should reject with the error');
}, 'Acquiring a reader after return()ing from a stream that errors');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    },
  });

  // read the first two chunks, then cancel
  const chunks = [];
  for await (const chunk of s) {
    chunks.push(chunk);
    if (chunk >= 2) {
      break;
    }
  }
  assert_array_equals(chunks, [1, 2]);

  const reader = s.getReader();
  await reader.closed;
}, 'Acquiring a reader after partially async-iterating a stream');

promise_test(async () => {
  const s = new ReadableStream({
    start(c) {
      c.enqueue(1);
      c.enqueue(2);
      c.enqueue(3);
      c.close();
    },
  });

  // read the first two chunks, then release lock
  const chunks = [];
  for await (const chunk of s.values({preventCancel: true})) {
    chunks.push(chunk);
    if (chunk >= 2) {
      break;
    }
  }
  assert_array_equals(chunks, [1, 2]);

  const reader = s.getReader();
  const readResult = await reader.read();
  assert_iter_result(readResult, 3, false);
  await reader.closed;
}, 'Acquiring a reader and reading the remaining chunks after partially async-iterating a stream with preventCancel = true');

for (const preventCancel of [false, true]) {
  test(() => {
    const rs = new ReadableStream();
    rs.values({ preventCancel }).return();
    // The test passes if this line doesn't throw.
    rs.getReader();
  }, `return() should unlock the stream synchronously when preventCancel = ${preventCancel}`);
}

promise_test(async () => {
  const rs = new ReadableStream({
    async start(c) {
      c.enqueue('a');
      c.enqueue('b');
      c.enqueue('c');
      await flushAsyncEvents();
      // At this point, the async iterator has a read request in the stream's queue for its pending next() promise.
      // Closing the stream now causes two things to happen *synchronously*:
      //  1. ReadableStreamClose resolves reader.[[closedPromise]] with undefined.
      //  2. ReadableStreamClose calls the read request's close steps, which calls ReadableStreamReaderGenericRelease,
      //     which replaces reader.[[closedPromise]] with a rejected promise.
      c.close();
    }
  });

  const chunks = [];
  for await (const chunk of rs) {
    chunks.push(chunk);
  }
  assert_array_equals(chunks, ['a', 'b', 'c']);
}, 'close() while next() is pending');

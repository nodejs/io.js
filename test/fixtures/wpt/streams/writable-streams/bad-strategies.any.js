// META: global=window,worker,shadowrealm
'use strict';

const error1 = new Error('a unique string');
error1.name = 'error1';

test(() => {
  assert_throws_exactly(error1, () => {
    new WritableStream({}, {
      get size() {
        throw error1;
      },
      highWaterMark: 5
    });
  }, 'construction should re-throw the error');
}, 'Writable stream: throwing strategy.size getter');

test(() => {
  assert_throws_js(TypeError, () => {
    new WritableStream({}, { size: 'a string' });
  });
}, 'reject any non-function value for strategy.size');

test(() => {
  assert_throws_exactly(error1, () => {
    new WritableStream({}, {
      size() {
        return 1;
      },
      get highWaterMark() {
        throw error1;
      }
    });
  }, 'construction should re-throw the error');
}, 'Writable stream: throwing strategy.highWaterMark getter');

test(() => {

  for (const highWaterMark of [-1, -Infinity, NaN, 'foo', {}]) {
    assert_throws_js(RangeError, () => {
      new WritableStream({}, {
        size() {
          return 1;
        },
        highWaterMark
      });
    }, `construction should throw a RangeError for ${highWaterMark}`);
  }
}, 'Writable stream: invalid strategy.highWaterMark');

promise_test(t => {
  const ws = new WritableStream({}, {
    size() {
      throw error1;
    },
    highWaterMark: 5
  });

  const writer = ws.getWriter();

  const p1 = promise_rejects_exactly(t, error1, writer.write('a'), 'write should reject with the thrown error');

  const p2 = promise_rejects_exactly(t, error1, writer.closed, 'closed should reject with the thrown error');

  return Promise.all([p1, p2]);
}, 'Writable stream: throwing strategy.size method');

promise_test(() => {
  const sizes = [NaN, -Infinity, Infinity, -1];
  return Promise.all(sizes.map(size => {
    const ws = new WritableStream({}, {
      size() {
        return size;
      },
      highWaterMark: 5
    });

    const writer = ws.getWriter();

    return writer.write('a').then(() => assert_unreached('write must reject'), writeE => {
      assert_equals(writeE.name, 'RangeError', `write must reject with a RangeError for ${size}`);

      return writer.closed.then(() => assert_unreached('write must reject'), closedE => {
        assert_equals(closedE, writeE, `closed should reject with the same error as write`);
      });
    });
  }));
}, 'Writable stream: invalid strategy.size return value');

test(() => {
  assert_throws_js(TypeError, () => new WritableStream(undefined, {
    size: 'not a function',
    highWaterMark: NaN
  }), 'WritableStream constructor should throw a TypeError');
}, 'Writable stream: invalid size beats invalid highWaterMark');

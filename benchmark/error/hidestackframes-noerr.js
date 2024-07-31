'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  type: [
    'hide-stackframes',
    'direct-call',
  ],
  n: [1e7],
}, {
  flags: ['--expose-internals'],
});

function main({ n, type }) {
  const {
    hideStackFrames,
    codes: {
      ERR_INVALID_ARG_TYPE,
    },
  } = require('internal/errors');

  const testfn = (value) => {
    if (typeof value !== 'number') {
      throw new ERR_INVALID_ARG_TYPE('Benchmark', 'number', value);
    }
  };

  const hideStackFramesTestfn = hideStackFrames((value) => {
    if (typeof value !== 'number') {
      throw new ERR_INVALID_ARG_TYPE.HideStackFrameError('Benchmark', 'number', value);
    }
  });

  const fn = type === 'hide-stackframe' ? hideStackFramesTestfn : testfn;

  const value = 42;

  const length = 1024;
  const array = [];
  const errCase = false;

  for (let i = 0; i < length; ++i) {
    array.push(fn(value));
  }

  bench.start();

  for (let i = 0; i < n; i++) {
    const index = i % length;
    array[index] = fn(value);
  }

  bench.end(n);

  // Verify the entries to prevent dead code elimination from making
  // the benchmark invalid.
  for (let i = 0; i < length; ++i) {
    assert.strictEqual(typeof array[i], errCase ? 'object' : 'undefined');
  }
}

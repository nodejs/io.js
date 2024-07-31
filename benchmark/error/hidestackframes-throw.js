'use strict';

const common = require('../common.js');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  type: [
    'hide-stackframes',
    'direct-call',
  ],
  double: ['true', 'false'],
  n: [1e5],
}, {
  flags: ['--expose-internals'],
});

function main({ n, type, double }) {
  const {
    hideStackFrames,
    codes: {
      ERR_INVALID_ARG_TYPE,
    },
  } = require('internal/errors');

  const value = 'err';

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

  function doubleTestfn(value) {
    testfn(value);
  }

  const doubleHideStackFramesTestfn = hideStackFrames((value) => {
    hideStackFramesTestfn.withoutStackTrace(value);
  });

  const fn = type === 'hide-stackframe' ?
    double === 'true' ?
      doubleHideStackFramesTestfn :
      hideStackFramesTestfn :
    double === 'true' ?
      doubleTestfn :
      testfn;

  const length = 1024;
  const array = [];
  let errCase = false;

  // Warm up.
  for (let i = 0; i < length; ++i) {
    try {
      fn(value);
    } catch (e) {
      errCase = true;
      array.push(e);
    }
  }

  bench.start();

  for (let i = 0; i < n; i++) {
    const index = i % length;
    try {
      fn(value);
    } catch (e) {
      array[index] = e;
    }
  }

  bench.end(n);

  // Verify the entries to prevent dead code elimination from making
  // the benchmark invalid.
  for (let i = 0; i < length; ++i) {
    assert.strictEqual(typeof array[i], errCase ? 'object' : 'undefined');
  }
}

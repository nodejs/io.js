'use strict';

const common = require('../common');
const assert = require('assert');

const bench = common.createBenchmark(main, {
  n: [1e6],
  code: [
    'built-in',
    'ERR_FS_CP_DIR_TO_NON_DIR',
  ],
  stackTraceLimit: [0, 10],
}, {
  flags: ['--expose-internals'],
});

function getErrorFactory(code) {
  const {
    ERR_FS_CP_DIR_TO_NON_DIR,
  } = require('internal/errors').codes;

  switch (code) {
    case 'built-in':
      return (n) => new Error();
    case 'ERR_FS_CP_DIR_TO_NON_DIR':
      return (n) => new ERR_FS_CP_DIR_TO_NON_DIR({
        message: 'cannot overwrite directory',
        path: 'dest',
        syscall: 'cp',
        errno: 21,
        code: 'EISDIR',
      });
    default:
      throw new Error(`${code} not supported`);
  }
}

function main({ n, code, stackTraceLimit }) {
  const getError = getErrorFactory(code);

  Error.stackTraceLimit = stackTraceLimit;

  // Warm up.
  const length = 1024;
  const array = [];
  for (let i = 0; i < length; ++i) {
    array.push(getError(i));
  }

  bench.start();

  for (let i = 0; i < n; ++i) {
    const index = i % length;
    array[index] = getError(index);
  }

  bench.end(n);

  // Verify the entries to prevent dead code elimination from making
  // the benchmark invalid.
  for (let i = 0; i < length; ++i) {
    assert.strictEqual(typeof array[i], 'object');
  }
}

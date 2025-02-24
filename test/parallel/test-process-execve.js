'use strict';

const { isWindows, skip } = require('../common');
const { deepStrictEqual, fail, strictEqual } = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  skip('process.execve is not available in Workers');
} else if (isWindows) {
  skip('process.execve is not available in Windows');
}

if (process.argv[2] === 'replaced') {
  deepStrictEqual(process.argv, [process.execPath, __filename, 'replaced']);
  strictEqual(process.env.CWD, process.cwd());
  strictEqual(process.env.EXECVE_A, 'FIRST');
  strictEqual(process.env.EXECVE_B, 'SECOND');
} else {
  process.execve(
    process.execPath,
    [process.execPath, __filename, 'replaced'],
    { ...process.env, EXECVE_A: 'FIRST', EXECVE_B: 'SECOND', CWD: process.cwd() }
  );

  // If process.execve succeed, this should never be executed.
  fail('process.execve failed');
}

'use strict';

const common = require('../common');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.chdir is not available in Workers');
}
const assert = require('assert');

assert.throws(
  () => {
    process.chdir('does-not-exist');
  },
  {
    name: 'Error',
    code: 'ENOENT',
    message: /ENOENT: no such file or directory, chdir .+ -> 'does-not-exist'/,
    path: process.cwd(),
    syscall: 'chdir',
    dest: 'does-not-exist'
  }
);

'use strict';

const common = require('../common');
const fs = require('fs');

// A large number is selected to ensure that the fast path is called.
// Ref: https://github.com/nodejs/node/issues/53902
for (let i = 0; i < 100_000; i++) {
  try {
    fs.closeSync(100);
  } catch (e) {
    // Ignore all EBADF errors.
    // EBADF stands for "Bad file descriptor".
    if (e.code !== 'EBADF') {
      throw e;
    }
  }
}

// This test is to ensure that fs.close() does not crash under pressure.
for (let i = 0; i < 100_000; i++) {
  fs.close(100, common.mustCall());
}

// This test is to ensure that fs.readFile() does not crash.
// readFile() calls fs.close() internally if the input is not a file descriptor.
// Large number is selected to ensure that the fast path is called.
// Ref: https://github.com/nodejs/node/issues/53902
for (let i = 0; i < 100_000; i++) {
  fs.readFile(__filename, common.mustCall());
}

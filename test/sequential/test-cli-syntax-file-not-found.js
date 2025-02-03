'use strict';

const common = require('../common');
const assert = require('assert');
const { exec } = require('child_process');
const fixtures = require('../common/fixtures');

// Test both sets of arguments that check syntax
const syntaxArgs = [
  '-c',
  '--check',
];

const notFoundRE = /^Error: Cannot find module/m;

// test file not found
[
  'syntax/file_not_found.js',
  'syntax/file_not_found',
].forEach(function(file) {
  file = fixtures.path(file);

  // Loop each possible option, `-c` or `--check`
  syntaxArgs.forEach(function(flag) {
    exec(...common.escapePOSIXShell`"${process.execPath}" ${flag} "${file}"`, common.mustCall((err, stdout, stderr) => {
      // No stdout should be produced
      assert.strictEqual(stdout, '');

      // `stderr` should have a module not found error message.
      assert.match(stderr, notFoundRE);

      assert.strictEqual(err.code, 1,
                         `code ${err.code} !== 1 for error:\n\n${err}`);
    }));
  });
});

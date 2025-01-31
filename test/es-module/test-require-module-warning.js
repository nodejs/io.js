'use strict';

// This checks the warning and the stack trace emitted by --trace-require-module=all.
require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');
const assert = require('assert');

spawnSyncAndAssert(process.execPath, [
  '--trace-warnings',
  '--trace-require-module=all',
  fixtures.path('es-modules', 'require-module.js'),
], {
  trim: true,
  stderr(output) {
    const lines = output.split('\n');
    assert.match(
      lines[0],
      /ExperimentalWarning: CommonJS module .*require-module\.js is loading ES Module .*message\.mjs/
    );
    assert.strictEqual(
      lines[1],
      'Support for loading ES Module in require() is an experimental feature and might change at any time'
    );
    assert.match(
      lines[2],
      /at require \(.*modules\/helpers:\d+:\d+\)/
    );
    assert.match(
      lines[3],
      /at Object\.<anonymous> \(.*require-module\.js:1:1\)/
    );
  }
});

spawnSyncAndAssert(process.execPath, [
  '--trace-require-module=1',
  fixtures.path('es-modules', 'require-module.js'),
], {
  status: 9,
  trim: true,
  stderr: /invalid value for --trace-require-module/
});

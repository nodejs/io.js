// Flags: --experimental-require-module
'use strict';

require('../common');
const assert = require('assert');
const { spawnSyncAndExit } = require('../common/child_process');
const fixtures = require('../common/fixtures');

spawnSyncAndExit(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules/require-syntax-error.cjs'),
], {
  status: 1,
  signal: null,
  stderr(output) {
    assert.match(output, /var foo bar;/);
    assert.match(output, /SyntaxError: Unexpected identifier 'bar'/);
    return true;
  },
});

spawnSyncAndExit(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules/require-reference-error.cjs'),
], {
  status: 1,
  signal: null,
  trim: true,
  stdout: 'executed',
  stderr(output) {
    assert.match(output, /module\.exports = { hello: 'world' };/);
    assert.match(output, /ReferenceError: module is not defined/);
    return true;
  },
});

spawnSyncAndExit(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules/require-throw-error.cjs'),
], {
  status: 1,
  signal: null,
  stderr(output) {
    assert.match(output, /throw new Error\('test'\);/);
    assert.match(output, /Error: test/);
    return true;
  },
});

'use strict';

// This checks the warning and the stack trace emitted by
// --trace-require-module=no-node-modules.
require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');

const warningRE = /Support for loading ES Module in require\(\)/;

// The fixtures are placed in a directory that includes "node_modules" in its name
// to check false negatives.

// require() in non-node_modules -> esm in node_modules should warn.
spawnSyncAndAssert(
  process.execPath,
  [
    '--trace-require-module=no-node-modules',
    fixtures.path('es-modules', 'test_node_modules', 'require-esm.js'),
  ],
  {
    trim: true,
    stderr: warningRE,
    stdout: 'world',
  }
);

// require() in non-node_modules -> require() in node_modules -> esm in node_modules
// should not warn.
spawnSyncAndAssert(
  process.execPath,
  [
    '--trace-require-module=no-node-modules',
    fixtures.path('es-modules', 'test_node_modules', 'require-require-esm.js'),
  ],
  {
    trim: true,
    stderr: '',
    stdout: 'world',
  }
);

// Import in non-node_modules -> require() in node_modules -> esm in node_modules
// should not warn.
spawnSyncAndAssert(
  process.execPath,
  [
    '--trace-require-module=no-node-modules',
    fixtures.path('es-modules', 'test_node_modules', 'import-require-esm.mjs'),
  ],
  {
    trim: true,
    stderr: '',
    stdout: 'world',
  }
);

// Import in non-node_modules -> import in node_modules ->
// require() in node_modules -> esm in node_modules should not warn.
spawnSyncAndAssert(
  process.execPath,
  [
    '--trace-require-module=no-node-modules',
    fixtures.path('es-modules', 'test_node_modules', 'import-import-require-esm.mjs'),
  ],
  {
    trim: true,
    stderr: '',
    stdout: 'world',
  }
);

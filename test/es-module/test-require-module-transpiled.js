'use strict';
require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');

// This is a minimum integration test for CJS transpiled from ESM that tries to load real ESM.

spawnSyncAndAssert(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules', 'transpiled-cjs-require-module', 'dist', 'import-both.cjs'),
], {
  trim: true,
  stdout: 'import both',
});

spawnSyncAndAssert(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules', 'transpiled-cjs-require-module', 'dist', 'import-named.cjs'),
], {
  trim: true,
  stdout: 'import named',
});

spawnSyncAndAssert(process.execPath, [
  '--experimental-require-module',
  fixtures.path('es-modules', 'transpiled-cjs-require-module', 'dist', 'import-default.cjs'),
], {
  trim: true,
  stdout: 'import default',
});

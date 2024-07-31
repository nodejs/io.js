'use strict';

require('../common');
const { spawnSyncAndExit, spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');

// require-a.cjs -> a.mjs -> b.cjs -> a.mjs.
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-cycle/require-a.cjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot require\(\) ES Module .*a\.mjs in a cycle\. \(from .*require-a\.cjs\)/,
    }
  );
}

// require-b.cjs -> b.cjs -> a.mjs -> b.cjs.
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-cycle/require-b.cjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot import CommonJS Module \.\/b\.cjs in a cycle\. \(from .*a\.mjs\)/,
    }
  );
}

// a.mjs -> b.cjs -> a.mjs
{
  spawnSyncAndExit(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-cycle/a.mjs'),
    ],
    {
      signal: null,
      status: 1,
      stderr: /Cannot require\(\) ES Module .*a\.mjs in a cycle\. \(from .*b\.cjs\)/,
    }
  );
}

// b.cjs -> a.mjs -> b.cjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-cycle/b.cjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot import CommonJS Module \.\/b\.cjs in a cycle\. \(from .*a\.mjs\)/,
    }
  );
}

'use strict';

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');

// a.mjs -> b.cjs -> c.mjs -> a.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-esm-cycle/a.mjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot import Module \.\/a\.mjs in a cycle\. \(from .*c\.mjs\)/,
    }
  );
}

// b.cjs -> c.mjs -> a.mjs -> b.cjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-esm-cycle/b.cjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot import CommonJS Module \.\/b\.cjs in a cycle\. \(from .*a\.mjs\)/,
    }
  );
}

// c.mjs -> a.mjs -> b.cjs -> c.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-cjs-esm-esm-cycle/c.mjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stderr: /Cannot require\(\) ES Module .*c\.mjs in a cycle\. \(from .*b\.cjs\)/,
    }
  );
}

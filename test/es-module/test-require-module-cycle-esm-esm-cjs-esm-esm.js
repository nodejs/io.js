'use strict';

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');

// a.mjs -> b.mjs -> c.cjs -> z.mjs -> a.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-esm-cycle/a.mjs'),
    ],
    {
      signal: null,
      status: 1,
      stderr: /Cannot import Module \.\/a\.mjs in a cycle\. \(from .*z\.mjs\)/,
    }
  );
}

// b.mjs -> c.cjs -> z.mjs -> a.mjs -> b.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-esm-cycle/b.mjs'),
    ],
    {
      signal: null,
      status: 1,
      stderr: /Cannot import Module \.\/b\.mjs in a cycle\. \(from .*a\.mjs\)/,
    }
  );
}

// c.cjs -> z.mjs -> a.mjs -> b.mjs -> c.cjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-esm-cycle/c.cjs'),
    ],
    {
      signal: null,
      status: 1,
      stderr: /Cannot import CommonJS Module \.\/c\.cjs in a cycle\. \(from .*b\.mjs\)/,
    }
  );
}


// z.mjs -> a.mjs -> b.mjs -> c.cjs -> z.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-esm-cycle/z.mjs'),
    ],
    {
      signal: null,
      status: 1,
      stderr: /Cannot require\(\) ES Module .*z\.mjs in a cycle\. \(from .*c\.cjs\)/,
    }
  );
}

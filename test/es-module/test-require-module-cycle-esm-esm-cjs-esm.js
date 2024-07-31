'use strict';

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const fixtures = require('../common/fixtures');
const assert = require('assert');

// a.mjs -> b.mjs -> c.mjs -> d.mjs -> c.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-cycle/a.mjs'),
    ],
    {
      signal: null,
      status: 0,
      trim: true,
      stdout(output) {
        assert.match(output, /Start c/);
        assert.match(output, /dynamic import b\.mjs failed.*ERR_REQUIRE_CYCLE_MODULE/);
        assert.match(output, /dynamic import d\.mjs failed.*ERR_REQUIRE_CYCLE_MODULE/);
      }
    }
  );
}

// b.mjs -> c.mjs -> d.mjs -> c.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-cycle/b.mjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stdout: /Start c/,
      stderr: /Cannot import Module \.\/c\.mjs in a cycle\. \(from .*d\.mjs\)/,
    }
  );
}

// c.mjs -> d.mjs -> c.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-cycle/c.mjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stdout: /Start c/,
      stderr: /Cannot import Module \.\/c\.mjs in a cycle\. \(from .*d\.mjs\)/,
    }
  );
}


// d.mjs -> c.mjs -> d.mjs
{
  spawnSyncAndAssert(
    process.execPath,
    [
      '--experimental-require-module',
      fixtures.path('es-modules/esm-esm-cjs-esm-cycle/d.mjs'),
    ],
    {
      signal: null,
      status: 1,
      trim: true,
      stdout: /Start c/,
      stderr: /Cannot require\(\) ES Module .*d\.mjs in a cycle\. \(from .*c\.mjs\)/,
    }
  );
}

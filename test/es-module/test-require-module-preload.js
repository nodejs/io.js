'use strict';

require('../common');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const fixtures = require('../common/fixtures');

const stderr = /ExperimentalWarning: Support for loading ES Module in require/;

// Test named exports.
{
  spawnSyncAndExitWithoutError(
    process.execPath,
    [ '--experimental-require-module', '-r', fixtures.path('../fixtures/es-module-loaders/module-named-exports.mjs') ],
    {
      stderr,
    }
  );
}

// Test ESM that import ESM.
{
  spawnSyncAndExitWithoutError(
    process.execPath,
    [ '--experimental-require-module', '-r', fixtures.path('../fixtures/es-modules/import-esm.mjs') ],
    {
      stderr,
      stdout: 'world',
      trim: true,
    }
  );
}

// Test ESM that import CJS.
{
  spawnSyncAndExitWithoutError(
    process.execPath,
    [ '--experimental-require-module', '-r', fixtures.path('../fixtures/es-modules/cjs-exports.mjs') ],
    {
      stdout: 'ok',
      stderr,
      trim: true,
    }
  );
}

// Test ESM that require() CJS.
// Can't use the common/index.mjs here because that checks the globals, and
// -r injects a bunch of globals.
{
  spawnSyncAndExitWithoutError(
    process.execPath,
    [ '--experimental-require-module', '-r', fixtures.path('../fixtures/es-modules/require-cjs.mjs') ],
    {
      stdout: 'world',
      stderr,
      trim: true,
    }
  );
}

// Test "type": "module" and "main" field in package.json.
{
  spawnSyncAndExitWithoutError(
    process.execPath,
    [ '--experimental-require-module', '-r', fixtures.path('../fixtures/es-modules/package-type-module') ],
    {
      stdout: 'package-type-module',
      stderr,
      trim: true,
    }
  );
}

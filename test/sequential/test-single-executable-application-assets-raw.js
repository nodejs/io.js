'use strict';

const common = require('../common');

const {
  generateSEA,
  skipIfSingleExecutableIsNotSupported,
} = require('../common/sea');

skipIfSingleExecutableIsNotSupported();

// This tests the snapshot support in single executable applications.
const tmpdir = require('../common/tmpdir');

const { copyFileSync, writeFileSync, existsSync } = require('fs');
const {
  spawnSyncAndExitWithoutError,
} = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');

tmpdir.refresh();
if (!tmpdir.hasEnoughSpace(120 * 1024 * 1024)) {
  common.skip('Not enough disk space');
}

const configFile = tmpdir.resolve('sea-config.json');
const seaPrepBlob = tmpdir.resolve('sea-prep.blob');
const outputFile = tmpdir.resolve(process.platform === 'win32' ? 'sea.exe' : 'sea');

{
  tmpdir.refresh();
  copyFileSync(fixtures.path('sea', 'get-asset-raw.js'), tmpdir.resolve('sea.js'));
  copyFileSync(fixtures.path('person.jpg'), tmpdir.resolve('person.jpg'));
  writeFileSync(configFile, `
  {
    "main": "sea.js",
    "output": "sea-prep.blob",
    "assets": {
      "person.jpg": "person.jpg"
    }
  }
  `, 'utf8');

  spawnSyncAndExitWithoutError(
    process.execPath,
    ['--experimental-sea-config', 'sea-config.json'],
    {
      env: {
        NODE_DEBUG_NATIVE: 'SEA',
        ...process.env,
      },
      cwd: tmpdir.path
    });

  assert(existsSync(seaPrepBlob));

  generateSEA(outputFile, process.execPath, seaPrepBlob);

  spawnSyncAndExitWithoutError(
    outputFile,
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'SEA',
        __TEST_PERSON_JPG: fixtures.path('person.jpg'),
      }
    },
  );
}

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
  spawnSyncAndAssert,
  spawnSyncAndExit,
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
  copyFileSync(fixtures.path('sea', 'get-asset.js'), tmpdir.resolve('sea.js'));
  writeFileSync(configFile, `
  {
    "main": "sea.js",
    "output": "sea-prep.blob",
    "assets": "invalid"
  }
  `);

  spawnSyncAndExit(
    process.execPath,
    ['--experimental-sea-config', 'sea-config.json'],
    {
      cwd: tmpdir.path
    },
    {
      status: 1,
      signal: null,
      stderr: /"assets" field of sea-config\.json is not a map of strings/
    });
}

{
  tmpdir.refresh();
  copyFileSync(fixtures.path('sea', 'get-asset.js'), tmpdir.resolve('sea.js'));
  writeFileSync(configFile, `
  {
    "main": "sea.js",
    "output": "sea-prep.blob",
    "assets": {
      "nonexistent": "nonexistent.txt"
    }
  }
  `);

  spawnSyncAndExit(
    process.execPath,
    ['--experimental-sea-config', 'sea-config.json'],
    {
      cwd: tmpdir.path
    },
    {
      status: 1,
      signal: null,
      stderr: /Cannot read asset nonexistent\.txt: no such file or directory/
    });
}

{
  tmpdir.refresh();
  copyFileSync(fixtures.path('sea', 'get-asset.js'), tmpdir.resolve('sea.js'));
  copyFileSync(fixtures.utf8TestTextPath, tmpdir.resolve('utf8_test_text.txt'));
  copyFileSync(fixtures.path('person.jpg'), tmpdir.resolve('person.jpg'));
  writeFileSync(configFile, `
  {
    "main": "sea.js",
    "output": "sea-prep.blob",
    "assets": {
      "utf8_test_text.txt": "utf8_test_text.txt",
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

  spawnSyncAndAssert(
    outputFile,
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'SEA',
        __TEST_PERSON_JPG: fixtures.path('person.jpg'),
        __TEST_UTF8_TEXT_PATH: fixtures.path('utf8_test_text.txt'),
      }
    },
    {
      trim: true,
      stdout: fixtures.utf8TestText,
    }
  );
}

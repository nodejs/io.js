'use strict';

require('../common');

const {
  generateSEA,
  skipIfSingleExecutableIsNotSupported,
} = require('../common/sea');

skipIfSingleExecutableIsNotSupported();

// This tests the snapshot support in single executable applications.

const tmpdir = require('../common/tmpdir');
const { writeFileSync, existsSync } = require('fs');
const {
  spawnSyncAndAssert,
  spawnSyncAndExit,
} = require('../common/child_process');
const assert = require('assert');

const configFile = tmpdir.resolve('sea-config.json');
const seaPrepBlob = tmpdir.resolve('sea-prep.blob');
const outputFile = tmpdir.resolve(process.platform === 'win32' ? 'sea.exe' : 'sea');

{
  tmpdir.refresh();

  writeFileSync(tmpdir.resolve('snapshot.js'), '', 'utf-8');
  writeFileSync(configFile, `
  {
    "main": "snapshot.js",
    "output": "sea-prep.blob",
    "useSnapshot": true
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
      stderr: /snapshot\.js does not invoke v8\.startupSnapshot\.setDeserializeMainFunction\(\)/
    });
}

{
  tmpdir.refresh();
  const code = `
  const {
    setDeserializeMainFunction,
  } = require('v8').startupSnapshot;
  
  setDeserializeMainFunction(() => {
    console.log('Hello from snapshot');
  });
  `;

  writeFileSync(tmpdir.resolve('snapshot.js'), code, 'utf-8');
  writeFileSync(configFile, `
  {
    "main": "snapshot.js",
    "output": "sea-prep.blob",
    "useSnapshot": true
  }
  `);

  spawnSyncAndAssert(
    process.execPath,
    ['--experimental-sea-config', 'sea-config.json'],
    {
      cwd: tmpdir.path,
      env: {
        NODE_DEBUG_NATIVE: 'SEA',
        ...process.env,
      },
    },
    {
      stderr: /Single executable application is an experimental feature/
    });

  assert(existsSync(seaPrepBlob));

  generateSEA(outputFile, process.execPath, seaPrepBlob);

  spawnSyncAndAssert(
    outputFile,
    {
      env: {
        NODE_DEBUG_NATIVE: 'SEA,MKSNAPSHOT',
        ...process.env,
      }
    },
    {
      trim: true,
      stdout: 'Hello from snapshot',
      stderr(output) {
        assert.doesNotMatch(
          output,
          /Single executable application is an experimental feature/);
      }
    }
  );
}

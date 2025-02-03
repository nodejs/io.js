'use strict';

const {
  setDeserializeMainFunction,
  isBuildingSnapshot
} = require('v8').startupSnapshot;

function spawn() {
  const { spawnSync, execFileSync, execSync } = require('child_process');
  spawnSync(process.execPath, [ __filename, 'spawnSync' ], { stdio: 'inherit' });
  if (!process.env.DIRNAME_CONTAINS_SHELL_UNSAFE_CHARS)
    execSync(`"${process.execPath}" "${__filename}" "execSync"`, { stdio: 'inherit' });
  execFileSync(process.execPath, [ __filename, 'execFileSync' ], { stdio: 'inherit' });
}

if (process.argv[2] !== undefined) {
  console.log('From child process', process.argv[2]);
} else {
  spawn();
}

if (isBuildingSnapshot()) {
  setDeserializeMainFunction(() => {
    spawn();
  });
}

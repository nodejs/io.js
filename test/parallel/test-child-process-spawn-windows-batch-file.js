'use strict';

// Node.js on Windows should not be able to spawn batch files directly,
// only when the 'shell' option is set. An undocumented feature of the
// Win32 CreateProcess API allows spawning .bat and .cmd files directly
// but it does not sanitize arguments. We cannot do that automatically
// because it's sometimes impossible to escape arguments unambiguously.
//
// Expectation: spawn() and spawnSync() raise EINVAL if and only if:
//
// 1. 'shell' option is unset
// 2. Platform is Windows
// 3. Filename ends in .bat or .cmd, case-insensitive
//
// exec() and execSync() are unchanged.

const common = require('../common');
const cp = require('child_process');
const assert = require('assert');
const { isWindows } = common;

const expectedCode = isWindows ? 'EINVAL' : 'ENOENT';
const expectedStatus = isWindows ? 1 : 127;

const suffixes =
    'BAT|bAT|BaT|baT|BAt|bAt|Bat|bat|CMD|cMD|CmD|cmD|CMd|cMd|Cmd|cmd|cmd |cmd .|cmd ....'
    .split('|');

function testExec(filename) {
  return new Promise((resolve) => {
    cp.exec(filename).once('exit', common.mustCall(function(status) {
      assert.strictEqual(status, expectedStatus);
      resolve();
    }));
  });
}

function testExecSync(filename) {
  let e;
  try {
    cp.execSync(filename);
  } catch (_e) {
    e = _e;
  }
  if (!e) throw new Error(`Exception expected for ${filename}`);
  assert.strictEqual(e.status, expectedStatus);
}

function testSpawn(filename, code) {
  // Batch file case is a synchronous error, file-not-found is asynchronous.
  if (code === 'EINVAL') {
    let e;
    try {
      cp.spawn(filename);
    } catch (_e) {
      e = _e;
    }
    if (!e) throw new Error(`Exception expected for ${filename}`);
    assert.strictEqual(e.code, code);
  } else {
    return new Promise((resolve) => {
      cp.spawn(filename).once('error', common.mustCall(function(e) {
        assert.strictEqual(e.code, code);
        resolve();
      }));
    });
  }
}

function testSpawnSync(filename, code) {
  {
    const r = cp.spawnSync(filename);
    assert.strictEqual(r.error.code, code);
  }
  {
    const r = cp.spawnSync(filename, { shell: true });
    assert.strictEqual(r.status, expectedStatus);
  }
}

testExecSync('./nosuchdir/nosuchfile');
testSpawnSync('./nosuchdir/nosuchfile', 'ENOENT');
for (const suffix of suffixes) {
  testExecSync(`./nosuchdir/nosuchfile.${suffix}`);
  testSpawnSync(`./nosuchdir/nosuchfile.${suffix}`, expectedCode);
}

go().catch((ex) => { throw ex; });

async function go() {
  await testExec('./nosuchdir/nosuchfile');
  await testSpawn('./nosuchdir/nosuchfile', 'ENOENT');
  for (const suffix of suffixes) {
    await testExec(`./nosuchdir/nosuchfile.${suffix}`);
    await testSpawn(`./nosuchdir/nosuchfile.${suffix}`, expectedCode);
  }
}

'use strict';

const assert = require('assert');
const { spawnSync, execFileSync } = require('child_process');
const common = require('./');
const util = require('util');

// Workaround for Windows Server 2008R2
// When CMD is used to launch a process and CMD is killed too quickly, the
// process can stay behind running in suspended state, never completing.
function cleanupStaleProcess(filename) {
  if (!common.isWindows) {
    return;
  }
  process.once('beforeExit', () => {
    const basename = filename.replace(/.*[/\\]/g, '');
    try {
      execFileSync(`${process.env.SystemRoot}\\System32\\wbem\\WMIC.exe`, [
        'process',
        'where',
        `commandline like '%${basename}%child'`,
        'delete',
        '/nointeractive',
      ]);
    } catch {
      // Ignore failures, there might not be any stale process to clean up.
    }
  });
}

// This should keep the child process running long enough to expire
// the timeout.
const kExpiringChildRunTime = common.platformTimeout(20 * 1000);
const kExpiringParentTimer = 1;
assert(kExpiringChildRunTime > kExpiringParentTimer);

function logAfterTime(time) {
  setTimeout(() => {
    // The following console statements are part of the test.
    console.log('child stdout');
    console.error('child stderr');
  }, time);
}

function checkOutput(str, check) {
  if ((check instanceof RegExp && !check.test(str)) ||
    (typeof check === 'string' && check !== str)) {
    return { passed: false, reason: `did not match ${util.inspect(check)}` };
  }
  if (typeof check === 'function') {
    try {
      check(str);
    } catch (error) {
      return {
        passed: false,
        reason: `did not match expectation, checker throws:\n${util.inspect(error)}`,
      };
    }
  }
  return { passed: true };
}

function expectSyncExit(caller, spawnArgs, {
  status,
  signal,
  stderr: stderrCheck,
  stdout: stdoutCheck,
  trim = false,
}) {
  const child = spawnSync(...spawnArgs);
  const failures = [];
  let stderrStr, stdoutStr;
  if (status !== undefined && child.status !== status) {
    failures.push(`- process terminated with status ${child.status}, expected ${status}`);
  }
  if (signal !== undefined && child.signal !== signal) {
    failures.push(`- process terminated with signal ${child.signal}, expected ${signal}`);
  }

  function logAndThrow() {
    const tag = `[process ${child.pid}]:`;
    console.error(`${tag} --- stderr ---`);
    console.error(stderrStr === undefined ? child.stderr.toString() : stderrStr);
    console.error(`${tag} --- stdout ---`);
    console.error(stdoutStr === undefined ? child.stdout.toString() : stdoutStr);
    console.error(`${tag} status = ${child.status}, signal = ${child.signal}`);

    const error = new Error(`${failures.join('\n')}`);
    if (spawnArgs[2]) {
      error.options = spawnArgs[2];
    }
    let command = spawnArgs[0];
    if (Array.isArray(spawnArgs[1])) {
      command += ' ' + spawnArgs[1].join(' ');
    }
    error.command = command;
    Error.captureStackTrace(error, caller);
    throw error;
  }

  // If status and signal are not matching expectations, fail early.
  if (failures.length !== 0) {
    logAndThrow();
  }

  if (stderrCheck !== undefined) {
    stderrStr = child.stderr.toString();
    const { passed, reason } = checkOutput(trim ? stderrStr.trim() : stderrStr, stderrCheck);
    if (!passed) {
      failures.push(`- stderr ${reason}`);
    }
  }
  if (stdoutCheck !== undefined) {
    stdoutStr = child.stdout.toString();
    const { passed, reason } = checkOutput(trim ? stdoutStr.trim() : stdoutStr, stdoutCheck);
    if (!passed) {
      failures.push(`- stdout ${reason}`);
    }
  }
  if (failures.length !== 0) {
    logAndThrow();
  }
  return { child, stderr: stderrStr, stdout: stdoutStr };
}

function spawnSyncAndExit(...args) {
  const spawnArgs = args.slice(0, args.length - 1);
  const expectations = args[args.length - 1];
  return expectSyncExit(spawnSyncAndExit, spawnArgs, expectations);
}

function spawnSyncAndExitWithoutError(...args) {
  return expectSyncExit(spawnSyncAndExitWithoutError, [...args], {
    status: 0,
    signal: null,
  });
}

function spawnSyncAndAssert(...args) {
  const expectations = args.pop();
  return expectSyncExit(spawnSyncAndAssert, [...args], {
    status: 0,
    signal: null,
    ...expectations,
  });
}

module.exports = {
  cleanupStaleProcess,
  logAfterTime,
  kExpiringChildRunTime,
  kExpiringParentTimer,
  spawnSyncAndAssert,
  spawnSyncAndExit,
  spawnSyncAndExitWithoutError,
};

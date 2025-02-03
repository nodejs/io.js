'use strict';

const assert = require('assert');

function getTestCases(isWorker = false) {
  const cases = [];
  function exitsOnExitCodeSet() {
    process.exitCode = 42;
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 42);
      assert.strictEqual(code, 42);
    });
  }
  cases.push({ func: exitsOnExitCodeSet, result: 42 });

  function changesCodeViaExit() {
    process.exitCode = 99;
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 42);
      assert.strictEqual(code, 42);
    });
    process.exit(42);
  }
  cases.push({ func: changesCodeViaExit, result: 42 });

  function changesCodeZeroExit() {
    process.exitCode = 99;
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 0);
      assert.strictEqual(code, 0);
    });
    process.exit(0);
  }
  cases.push({ func: changesCodeZeroExit, result: 0 });

  function exitWithOneOnUncaught() {
    process.exitCode = 99;
    process.on('exit', (code) => {
      // Cannot use assert because it will be uncaughtException -> 1 exit code
      // that will render this test useless
      if (code !== 1 || process.exitCode !== 1) {
        console.log('wrong code! expected 1 for uncaughtException');
        process.exit(99);
      }
    });
    throw new Error('ok');
  }
  cases.push({
    func: exitWithOneOnUncaught,
    result: 1,
    error: /^Error: ok$/,
  });

  function changeCodeInsideExit() {
    process.exitCode = 95;
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 95);
      assert.strictEqual(code, 95);
      process.exitCode = 99;
    });
  }
  cases.push({ func: changeCodeInsideExit, result: 99 });

  function zeroExitWithUncaughtHandler() {
    const noop = () => { };
    process.on('exit', (code) => {
      process.off('uncaughtException', noop);
      assert.strictEqual(process.exitCode, undefined);
      assert.strictEqual(code, 0);
    });
    process.on('uncaughtException', noop);
    throw new Error('ok');
  }
  cases.push({ func: zeroExitWithUncaughtHandler, result: 0 });

  function changeCodeInUncaughtHandler() {
    const modifyExitCode = () => { process.exitCode = 97; };
    process.on('exit', (code) => {
      process.off('uncaughtException', modifyExitCode);
      assert.strictEqual(process.exitCode, 97);
      assert.strictEqual(code, 97);
    });
    process.on('uncaughtException', modifyExitCode);
    throw new Error('ok');
  }
  cases.push({ func: changeCodeInUncaughtHandler, result: 97 });

  function changeCodeInExitWithUncaught() {
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 1);
      assert.strictEqual(code, 1);
      process.exitCode = 98;
    });
    throw new Error('ok');
  }
  cases.push({
    func: changeCodeInExitWithUncaught,
    result: 98,
    error: /^Error: ok$/,
  });

  function exitWithZeroInExitWithUncaught() {
    process.on('exit', (code) => {
      assert.strictEqual(process.exitCode, 1);
      assert.strictEqual(code, 1);
      process.exitCode = 0;
    });
    throw new Error('ok');
  }
  cases.push({
    func: exitWithZeroInExitWithUncaught,
    result: 0,
    error: /^Error: ok$/,
  });

  function exitWithThrowInUncaughtHandler() {
    process.on('uncaughtException', () => {
      throw new Error('ok');
    });
    throw new Error('bad');
  }
  cases.push({
    func: exitWithThrowInUncaughtHandler,
    result: isWorker ? 1 : 7,
    error: /^Error: ok$/,
  });

  function exitWithUndefinedFatalException() {
    process._fatalException = undefined;
    throw new Error('ok');
  }
  cases.push({
    func: exitWithUndefinedFatalException,
    result: 6,
  });
  return cases;
}
exports.getTestCases = getTestCases;

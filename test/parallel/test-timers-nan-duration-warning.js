'use strict';

require('../common');
const assert = require('assert');
const child_process = require('child_process');
const path = require('path');

const NOT_A_NUMBER = NaN;

function timerNotCanceled() {
  assert.fail('Timer should be canceled');
}

const testCases = ['timeout', 'interval', 'refresh'];

function runTests() {
  const args = process.argv.slice(2);

  const testChoice = args[0];

  if (!testChoice) {
    const filePath = path.join(__filename);

    testCases.forEach((testCase) => {
      const { stdout } = child_process.spawnSync(
        process.execPath,
        [filePath, testCase],
        { encoding: 'utf8' }
      );

      const lines = stdout.split('\n');

      if (lines[0] === 'DeprecationWarning') return;

      assert.strictEqual(lines[0], 'TimeoutNaNWarning');
      assert.strictEqual(lines[1], `${NOT_A_NUMBER} is not a number.`);
      assert.strictEqual(lines[2], 'Timeout duration was set to 1.');
    });
  }

  if (args[0] === testCases[0]) {
    const timeout = setTimeout(timerNotCanceled, NOT_A_NUMBER);
    clearTimeout(timeout);
  }

  if (args[0] === testCases[1]) {
    const interval = setInterval(timerNotCanceled, NOT_A_NUMBER);
    clearInterval(interval);
  }

  if (args[0] === testCases[2]) {
    const timeout = setTimeout(timerNotCanceled, NOT_A_NUMBER);
    timeout.refresh();
    clearTimeout(timeout);
  }

  process.on(
    'warning',

    (warning) => {
      console.log(warning.name);
      console.log(warning.message);
    }
  );
}

runTests();

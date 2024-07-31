'use strict';

const common = require('../common');
const assert = require('assert');

const NOT_A_NUMBER = NaN;

function timerNotCanceled() {
  assert.fail('Timer should be canceled');
}

process.on(
  'warning',
  common.mustCall((warning) => {
    if (warning.name === 'DeprecationWarning') return;

    const lines = warning.message.split('\n');

    assert.strictEqual(warning.name, 'TimeoutNaNWarning');
    assert.strictEqual(lines[0], `${NOT_A_NUMBER} is not a number.`);
    assert.strictEqual(lines.length, 2);
  }, 1)
);

{
  const timeout = setTimeout(timerNotCanceled, NOT_A_NUMBER);
  clearTimeout(timeout);
}

{
  const interval = setInterval(timerNotCanceled, NOT_A_NUMBER);
  clearInterval(interval);
}

{
  const timeout = setTimeout(timerNotCanceled, NOT_A_NUMBER);
  timeout.refresh();
  clearTimeout(timeout);
}

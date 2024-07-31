'use strict';

const common = require('../common');
const assert = require('assert');

const NEGATIVE_NUMBER = -1;

function timerNotCanceled() {
  assert.fail('Timer should be canceled');
}

process.on(
  'warning',
  common.mustCall((warning) => {
    if (warning.name === 'DeprecationWarning') return;

    const lines = warning.message.split('\n');

    assert.strictEqual(warning.name, 'TimeoutNegativeWarning');
    assert.strictEqual(lines[0], `${NEGATIVE_NUMBER} is a negative number.`);
    assert.strictEqual(lines.length, 2);
  }, 1)
);

{
  const timeout = setTimeout(timerNotCanceled, NEGATIVE_NUMBER);
  clearTimeout(timeout);
}

{
  const interval = setInterval(timerNotCanceled, NEGATIVE_NUMBER);
  clearInterval(interval);
}

{
  const timeout = setTimeout(timerNotCanceled, NEGATIVE_NUMBER);
  timeout.refresh();
  clearTimeout(timeout);
}

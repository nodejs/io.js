'use strict';

const common = require('../common');
const assert = require('assert');

function timerNotCanceled() {
  assert.fail('Timer should be canceled');
}

process.on(
  'warning',
  common.mustNotCall(() => {
    assert.fail('Timer should be canceled');
  })
);

{
  const timeout = setTimeout(timerNotCanceled, 0);
  clearTimeout(timeout);
}

{
  const interval = setInterval(timerNotCanceled, 0);
  clearInterval(interval);
}

{
  const timeout = setTimeout(timerNotCanceled, 0);
  timeout.refresh();
  clearTimeout(timeout);
}

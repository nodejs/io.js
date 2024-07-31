'use strict';
const common = require('../common');
const { test } = require('node:test');

// Regression test for https://github.com/nodejs/node/issues/51997
test('after hook should be called with no subtests', (t) => {
  const timer = setTimeout(common.mustNotCall(), 2 ** 30);

  t.after(common.mustCall(() => {
    clearTimeout(timer);
  }));
});

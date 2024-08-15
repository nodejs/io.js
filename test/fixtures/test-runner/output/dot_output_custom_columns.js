// Flags: --test-reporter=dot
'use strict';
process.stdout.columns = 30;
process.env.FORCE_COLOR = '1';
delete process.env.NODE_DISABLE_COLORS;
delete process.env.NO_COLOR;

const test = require('node:test');
const { setTimeout } = require('timers/promises');

for (let i = 0; i < 100; i++) {
  test(i + ' example', async () => {
    if (i === 0) {
      // So the reporter will run before all tests has started
      await setTimeout(10);
    }
    // resize
    if (i === 28)
      process.stdout.columns = 41;
  });
}

'use strict';
require('../common');
const assert = require('assert');

process.env.NODE_DISABLE_COLORS = '1';

// Does not show the indicator when the terminal is too narrow
process.stderr.columns = 20;

assert.throws(
  () => { assert.strictEqual('123456789ABCDEFGHI', '1!3!5!7!9!BC!!!GHI'); },
  (err) => !err.message.includes('^'),
);

// Does not show the indicator because the first difference is in the first 2 chars
process.stderr.columns = 80;
assert.throws(
  () => { assert.strictEqual('123456789ABCDEFGHI', '1!3!5!7!9!BC!!!GHI'); },
  {
    message: 'Expected values to be strictly equal:\n' +
      '+ actual - expected\n' +
      '\n' +
      "+ '123456789ABCDEFGHI'\n" +
      "- '1!3!5!7!9!BC!!!GHI'\n",
  },
);

// Show the indicator because the first difference is in the 3 char
process.stderr.columns = 80;
assert.throws(
  () => { assert.strictEqual('123456789ABCDEFGHI', '12!!5!7!9!BC!!!GHI'); },
  {
    message: 'Expected values to be strictly equal:\n' +
      '+ actual - expected\n' +
      '\n' +
      "+ '123456789ABCDEFGHI'\n" +
      "- '12!!5!7!9!BC!!!GHI'\n" +
      '     ^\n',
  },
);

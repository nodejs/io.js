'use strict';

require('../common');
const assert = require('assert');
const repl = require('repl');
const ArrayStream = require('../common/arraystream');

// eslint-disable-next-line no-control-regex
const clearChar = /\[1;1H\u001b\[0J>/;
let accum = '';
const output = new ArrayStream();
output.write = (data) => (accum += data.replace('\r', ''));

const r = repl.start({
  input: new ArrayStream(),
  output,
});
['new Error', 'Promise'].forEach((cmd) => r.write(`${cmd}\n`));
assert.strictEqual(accum.match(clearChar), null);
r.write('.cls\n');
assert.strictEqual(accum.match(clearChar).length > 0, true);
r.write('.exit\n');

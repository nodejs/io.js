// Flags: --expose-internals
'use strict';
const common = require('../common');
const { PassThrough } = require('stream');
const readline = require('readline');
const assert = require('assert');

const ctrlU = { ctrl: true, name: 'u' };

if (process.env.TERM === 'dumb') {
  common.skip('skipping - dumb terminal');
}

{
  const input = new PassThrough();
  const rl = readline.createInterface({
    terminal: true,
    input: input,
    prompt: ''
  });

  const tests = [
    [1, 'a'],
    [2, 'ab'],
    [2, '丁'],
    [0, '\u0301'],   // COMBINING ACUTE ACCENT
    [1, 'a\u0301'],  // á
    [0, '\u20DD'],   // COMBINING ENCLOSING CIRCLE
    [2, 'a\u20DDb'], // a⃝b
    [0, '\u200E'],   // LEFT-TO-RIGHT MARK
  ];

  for (const [cursor, string] of tests) {
    rl.write(string);
    assert.strictEqual(rl.getCursorPos().cols, cursor);
    rl.write(null, ctrlU);
  }
}

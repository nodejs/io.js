'use strict';

const common = require('../common.js');

const { styleText } = require('node:util');
const assert = require('node:assert');

const bench = common.createBenchmark(main, {
  messageType: ['string', 'number', 'boolean', 'invalid'],
  format: ['red', 'italic', 'invalid'],
  validateStream: [1, 0],
  n: [1e3],
});

function main({ messageType, format, validateStream, n }) {
  let str;
  switch (messageType) {
    case 'string':
      str = 'hello world';
      break;
    case 'number':
      str = 10;
      break;
    case 'boolean':
      str = true;
      break;
    case 'invalid':
      str = undefined;
      break;
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    let colored = '';
    try {
      colored = styleText(format, str, { validateStream });
      assert.ok(colored); // Attempt to avoid dead-code elimination
    } catch {
      // eslint-disable no-empty
    }
  }
  bench.end(n);
}

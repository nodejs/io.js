'use strict';

const common = require('../common.js');

const { styleText } = require('node:util');

const bench = common.createBenchmark(main, {
  messageType: ['string', 'number', 'boolean', 'invalid'],
  format: ['red', 'italic', 'invalid'],
  n: [1e3],
});

function main({ messageType, format, n }) {
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
    try {
      styleText(format, str);
    } catch {
      // eslint-disable no-empty
    }
  }
  bench.end(n);
}

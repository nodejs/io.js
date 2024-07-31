'use strict';
const common = require('../common.js');
const { Readable } = require('stream');

const bench = common.createBenchmark(main, {
  n: [1e6],
  kind: ['read', 'encoding'],
});
const ABC = new Uint8Array([0x41, 0x42, 0x43]);

function main({ n, kind }) {
  switch (kind) {
    case 'read': {
      bench.start();
      const readable = new Readable({
        read() {},
      });
      for (let i = 0; i < n; ++i) readable.push(ABC);
      bench.end(n);
      break;
    }

    case 'encoding': {
      bench.start();
      const readable = new Readable({
        read() {},
      });
      readable.setEncoding('utf8');
      for (let i = 0; i < n; ++i) readable.push(ABC);
      bench.end(n);
      break;
    }
    default:
      throw new Error('Invalid kind');
  }
}

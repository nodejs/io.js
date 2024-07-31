'use strict';
const common = require('../common.js');
const { Writable } = require('stream');

const bench = common.createBenchmark(main, {
  n: [50e6],
  kind: ['write', 'object-mode', 'writev'],
});
const ABC = new Uint8Array([0x41, 0x42, 0x43]);

function main({ n, kind }) {
  switch (kind) {
    case 'write': {
      bench.start();
      const wr = new Writable({
        write(chunk, encoding, cb) {
          cb();
        },
      });
      for (let i = 0; i < n; ++i) wr.write(ABC);
      bench.end(n);
      break;
    }

    case 'object-mode': {
      bench.start();
      const wr = new Writable({
        objectMode: true,
        write(chunk, encoding, cb) {
          cb();
        },
      });
      for (let i = 0; i < n; ++i) wr.write(ABC);
      bench.end(n);
      break;
    }
    case 'writev': {
      bench.start();
      const wr = new Writable({
        writev(chunks, cb) {
          cb();
        },
      });
      for (let i = 0; i < n; ++i) wr.write(ABC);
      bench.end(n);
      break;
    }
    default:
      throw new Error('Invalid kind');
  }
}

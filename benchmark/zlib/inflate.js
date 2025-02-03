'use strict';
const common = require('../common.js');
const zlib = require('zlib');

const bench = common.createBenchmark(main, {
  method: ['inflate', 'inflateSync'],
  inputLen: [1024],
  n: [4e5],
});

function main({ n, method, inputLen }) {
  // Default method value for tests.
  method ||= 'inflate';
  const chunk = zlib.deflateSync(Buffer.alloc(inputLen, 'a'));

  let i = 0;
  switch (method) {
    // Performs `n` single inflate operations
    case 'inflate': {
      const inflate = zlib.inflate;
      bench.start();
      (function next(err, result) {
        if (i++ === n)
          return bench.end(n);
        inflate(chunk, next);
      })();
      break;
    }
    // Performs `n` single inflateSync operations
    case 'inflateSync': {
      const inflateSync = zlib.inflateSync;
      bench.start();
      for (; i < n; ++i)
        inflateSync(chunk);
      bench.end(n);
      break;
    }
    default:
      throw new Error('Unsupported inflate method');
  }
}

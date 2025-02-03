'use strict';
const common = require('../common.js');
const zlib = require('zlib');

const bench = common.createBenchmark(main, {
  method: ['createDeflate', 'deflate', 'deflateSync'],
  inputLen: [1024],
  n: [4e5],
});

function main({ n, method, inputLen }) {
  // Default method value for testing.
  method ||= 'deflate';
  const chunk = Buffer.alloc(inputLen, 'a');

  switch (method) {
    // Performs `n` writes for a single deflate stream
    case 'createDeflate': {
      let i = 0;
      const deflater = zlib.createDeflate();
      deflater.resume();
      deflater.on('finish', () => {
        bench.end(n);
      });

      bench.start();
      (function next() {
        if (i++ === n)
          return deflater.end();
        deflater.write(chunk, next);
      })();
      break;
    }
    // Performs `n` single deflate operations
    case 'deflate': {
      let i = 0;
      const deflate = zlib.deflate;
      bench.start();
      (function next(err, result) {
        if (i++ === n)
          return bench.end(n);
        deflate(chunk, next);
      })();
      break;
    }
    // Performs `n` single deflateSync operations
    case 'deflateSync': {
      const deflateSync = zlib.deflateSync;
      bench.start();
      for (let i = 0; i < n; ++i)
        deflateSync(chunk);
      bench.end(n);
      break;
    }
    default:
      throw new Error('Unsupported deflate method');
  }
}

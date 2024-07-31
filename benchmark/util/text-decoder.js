'use strict';

const common = require('../common.js');

const bench = common.createBenchmark(main, {
  encoding: ['utf-8', 'latin1', 'iso-8859-3'],
  ignoreBOM: [0, 1],
  fatal: [0, 1],
  len: [256, 1024 * 16, 1024 * 128],
  n: [1e3],
  type: ['SharedArrayBuffer', 'ArrayBuffer', 'Buffer'],
});

function main({ encoding, len, n, ignoreBOM, type, fatal }) {
  const decoder = new TextDecoder(encoding, { ignoreBOM, fatal });
  let buf;

  switch (type) {
    case 'SharedArrayBuffer': {
      buf = new SharedArrayBuffer(len);
      break;
    }
    case 'ArrayBuffer': {
      buf = new ArrayBuffer(len);
      break;
    }
    case 'Buffer': {
      buf = Buffer.allocUnsafe(len);
      break;
    }
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    try {
      decoder.decode(buf);
    } catch {
      // eslint-disable no-empty
    }
  }
  bench.end(n);
}

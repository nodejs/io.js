'use strict';
const common = require('../common.js');

const {
  PassThrough,
  Readable,
  Writable,
  compose,
} = require('node:stream');

const bench = common.createBenchmark(main, {
  n: [1e3],
});

function main({ n }) {
  const cachedPassThroughs = [];
  const cachedReadables = [];
  const cachedWritables = [];

  for (let i = 0; i < n; i++) {
    const numberOfPassThroughs = 100;
    const passThroughs = [];

    for (let i = 0; i < numberOfPassThroughs; i++) {
      passThroughs.push(new PassThrough());
    }

    const readable = Readable.from(['hello', 'world']);
    const writable = new Writable({ objectMode: true, write: (chunk, encoding, cb) => cb() });

    cachedPassThroughs.push(passThroughs);
    cachedReadables.push(readable);
    cachedWritables.push(writable);
  }

  bench.start();
  for (let i = 0; i < n; i++) {
    const composed = compose(cachedReadables[i], ...cachedPassThroughs[i], cachedWritables[i]);
    composed.end();
  }
  bench.end(n);
}

'use strict';

const common = require('../common.js');
const { createHash } = require('crypto');
const { subtle } = globalThis.crypto;

const bench = common.createBenchmark(main, {
  sync: ['createHash', 'subtle'],
  data: [10, 20, 50, 100],
  method: ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'],
  n: [1e5],
});

const kMethods = {
  'SHA-1': 'sha1',
  'SHA-256': 'sha256',
  'SHA-384': 'sha384',
  'SHA-512': 'sha512',
};

// This benchmark only looks at clock time and ignores factors
// such as event loop delay, event loop utilization, and memory.
// As such, it is expected that the synchronous legacy method
// will always be faster in clock time.

function measureLegacy(n, data, method) {
  method = kMethods[method];
  bench.start();
  for (let i = 0; i < n; ++i) {
    createHash(method).update(data).digest();
  }
  bench.end(n);
}

function measureSubtle(n, data, method) {
  const ec = new TextEncoder();
  data = ec.encode(data);
  const jobs = new Array(n);
  bench.start();
  for (let i = 0; i < n; i++)
    jobs[i] = subtle.digest(method, data);
  Promise.all(jobs).then(() => bench.end(n)).catch((err) => {
    process.nextTick(() => { throw err; });
  });
}

function main({ n, sync, data, method }) {
  data = globalThis.crypto.getRandomValues(Buffer.alloc(data));
  switch (sync) {
    case 'createHash': return measureLegacy(n, data, method);
    case 'subtle': return measureSubtle(n, data, method);
  }
}

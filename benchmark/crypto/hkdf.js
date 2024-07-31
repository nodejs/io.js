'use strict';

const common = require('../common.js');
const assert = require('assert');
const {
  hkdf,
  hkdfSync,
} = require('crypto');

const bench = common.createBenchmark(main, {
  sync: [0, 1],
  size: [10, 64, 1024],
  key: ['a', 'secret', 'this-is-a-much-longer-secret'],
  salt: ['', 'salt'],
  info: ['', 'info'],
  hash: ['sha256', 'sha512'],
  n: [1e4],
});

function measureSync(n, size, salt, info, hash, key) {
  bench.start();
  for (let i = 0; i < n; ++i)
    hkdfSync(hash, key, salt, info, size);
  bench.end(n);
}

function measureAsync(n, size, salt, info, hash, key) {
  let remaining = n;
  function done(err) {
    assert.ifError(err);
    if (--remaining === 0)
      bench.end(n);
  }
  bench.start();
  for (let i = 0; i < n; ++i)
    hkdf(hash, key, salt, info, size, done);
}

function main({ n, sync, size, salt, info, hash, key }) {
  if (sync)
    measureSync(n, size, salt, info, hash, key);
  else
    measureAsync(n, size, salt, info, hash, key);
}

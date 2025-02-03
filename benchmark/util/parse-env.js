'use strict';

const { createBenchmark } = require('../common.js');
const path = require('node:path');
const fs = require('node:fs');
const util = require('node:util');
const assert = require('node:assert');

const bench = createBenchmark(main, {
  n: 3e4,
});

const env = fs.readFileSync(path.resolve(__dirname, '../fixtures/valid.env'), 'utf-8');

function main({ n }) {
  let noDead;
  bench.start();
  for (let i = 0; i < n; i++) {
    noDead = util.parseEnv(env);
  }
  bench.end(n);
  assert(noDead !== undefined);
}

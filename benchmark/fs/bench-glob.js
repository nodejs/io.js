'use strict';

const common = require('../common');
const fs = require('fs');
const path = require('path');
const assert = require('node:assert');

const benchmarkDirectory = path.resolve(__dirname, '..', '..');

const configs = {
  n: [1e3],
  dir: ['lib'],
  pattern: ['**/*', '*.js', '**/**.js'],
  mode: ['async', 'sync'],
  recursive: ['true', 'false'],
};

const bench = common.createBenchmark(main, configs);

async function main(config) {
  const fullPath = path.resolve(benchmarkDirectory, config.dir);
  const { pattern, recursive, mode } = config;

  let noDead;
  bench.start();

  for (let i = 0; i < config.n; i++) {
    if (mode === 'async') {
      noDead = await fs.promises.glob(pattern, { cwd: fullPath, recursive });
    } else {
      noDead = fs.globSync(pattern, { cwd: fullPath, recursive });
    }
  }

  bench.end(config.n);
  assert.ok(noDead);
}

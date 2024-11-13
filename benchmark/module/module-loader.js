'use strict';
const fs = require('fs');
const path = require('path');
const { builtinModules } = require('module');
const common = require('../common.js');

const tmpdir = require('../../test/common/tmpdir');
let benchmarkDirectory = tmpdir.resolve('nodejs-benchmark-module');

// Filter all irregular modules.
const otherModules = builtinModules.filter((name) => !/\/|^_|^sys/.test(name));

const bench = common.createBenchmark(main, {
  name: ['', '/', '/index.js'],
  dir: ['rel', 'abs'],
  files: [5e2],
  n: [1, 1e3],
  cache: ['true', 'false'],
}, { flags: '--no-warnings' });

function main({ n, name, cache, files, dir }) {
  tmpdir.refresh();
  fs.mkdirSync(benchmarkDirectory);
  for (let i = 0; i <= files; i++) {
    fs.mkdirSync(`${benchmarkDirectory}${i}`);
    fs.writeFileSync(
      `${benchmarkDirectory}${i}/package.json`,
      '{"main": "index.js"}',
    );
    fs.writeFileSync(
      `${benchmarkDirectory}${i}/index.js`,
      'module.exports = "";',
    );
  }

  if (dir === 'rel')
    benchmarkDirectory = path.relative(__dirname, benchmarkDirectory);

  measureDir(n, cache === 'true', files, name);

  tmpdir.refresh();
}

function measureDir(n, cache, files, name) {
  if (cache) {
    for (let i = 0; i <= files; i++) {
      require(`${benchmarkDirectory}${i}${name}`);
    }
  }
  bench.start();
  for (let i = 0; i <= files; i++) {
    for (let j = 0; j < n; j++)
      require(`${benchmarkDirectory}${i}${name}`);
    // Pretend mixed input (otherwise the results are less representative due to
    // highly specialized code).
    require(otherModules[i % otherModules.length]);
  }
  bench.end(n * files);
}

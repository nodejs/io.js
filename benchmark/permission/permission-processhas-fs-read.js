'use strict';
const common = require('../common.js');
const path = require('path');

const configs = {
  n: [1e5],
  concurrent: [1, 10],
};

const rootPath = path.resolve(__dirname, '../../..');

const options = {
  flags: [
    '--permission',
    `--allow-fs-read=${rootPath}`,
    '--allow-child-process',
    '--no-warnings',
  ],
};

const bench = common.createBenchmark(main, configs, options);

// This is a naive benchmark and might not demonstrate real-world use cases.
// New benchmarks will be created once the permission model config is available
// through a config file.
async function main(conf) {
  const benchmarkDir = path.join(__dirname, '../..');
  bench.start();

  for (let i = 0; i < conf.n; i++) {
    // Valid file in a sequence of denied files
    process.permission.has('fs.read', benchmarkDir + '/valid-file');
    // Denied file
    process.permission.has('fs.read', __filename);
    // Valid file a granted directory
    process.permission.has('fs.read', '/tmp/example');
  }

  bench.end(conf.n);
}

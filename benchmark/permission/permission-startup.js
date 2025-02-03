'use strict';

const common = require('../common.js');
const path = require('path');
const { spawnSync } = require('child_process');

function mockFiles(n, prefix = '/tmp') {
  const files = [];
  for (let i = 0; i < n; ++i) {
    files.push(prefix + '/file' + i + '.js');
  }

  return files;
}

const bench = common.createBenchmark(main, {
  script: [
    'test/fixtures/semicolon',
  ],
  prefixPath: ['/tmp'],
  nFiles: [10, 100, 1000],
  count: [30],
});

function spawnProcess(script, bench, state) {
  const cmd = process.execPath || process.argv[0];
  while (state.finished < state.count) {
    const child = spawnSync(cmd, script);
    if (child.status !== 0) {
      console.log('---- STDOUT ----');
      console.log(child.stdout.toString());
      console.log('---- STDERR ----');
      console.log(child.stderr.toString());
      throw new Error(`Child process stopped with exit code ${child.status}`);
    }
    state.finished++;
    if (state.finished === 0) {
      // Finished warmup.
      bench.start();
    }

    if (state.finished === state.count) {
      bench.end(state.count);
    }
  }
}

function main({ count, script, nFiles, prefixPath }) {
  script = path.resolve(__dirname, '../../', `${script}.js`);
  const optionsWithScript = [
    '--permission',
    `--allow-fs-read=${script}`,
    ...mockFiles(nFiles, prefixPath).map((file) => '--allow-fs-read=' + file),
    script,
  ];
  const warmup = 3;
  const state = { count, finished: -warmup };
  spawnProcess(optionsWithScript, bench, state);
}

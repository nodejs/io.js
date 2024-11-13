'use strict';
const common = require('../common.js');
const { spawnSync } = require('child_process');
const path = require('path');

let Worker;  // Lazy loaded in main

const bench = common.createBenchmark(main, {
  script: [
    'benchmark/fixtures/require-builtins',
    'test/fixtures/semicolon',
    'test/fixtures/snapshot/typescript',
  ],
  mode: ['process', 'worker'],
  n: [30],
});

function spawnProcess(script, bench, state) {
  const cmd = process.execPath || process.argv[0];
  while (state.finished < state.n) {
    const child = spawnSync(cmd, [script]);
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

    if (state.finished === state.n) {
      bench.end(state.n);
    }
  }
}

function spawnWorker(script, bench, state) {
  const child = new Worker(script);
  child.on('exit', (code) => {
    if (code !== 0) {
      throw new Error(`Worker stopped with exit code ${code}`);
    }
    state.finished++;
    if (state.finished === 0) {
      // Finished warmup.
      bench.start();
    }
    if (state.finished < state.n) {
      spawnWorker(script, bench, state);
    } else {
      bench.end(state.n);
    }
  });
}

function main({ n, script, mode }) {
  script = path.resolve(__dirname, '../../', `${script}.js`);
  const warmup = 3;
  const state = { n, finished: -warmup };
  if (mode === 'worker') {
    Worker = require('worker_threads').Worker;
    spawnWorker(script, bench, state);
  } else {
    spawnProcess(script, bench, state);
  }
}

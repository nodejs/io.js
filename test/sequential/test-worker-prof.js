'use strict';
const common = require('../common');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');
const assert = require('assert');
const { spawnSyncAndAssert } = require('../common/child_process');

// Test that --prof also tracks Worker threads.
// Refs: https://github.com/nodejs/node/issues/24016

if (process.argv[2] === 'child') {
  let files = fs.readdirSync(process.cwd());
  console.log('files found in working directory before worker starts', files);
  const parentProf = files.find((name) => name.endsWith('.log'));
  assert(parentProf, '--prof did not produce a profile log for parent thread');
  console.log('parent prof file:', parentProf);

  const { Worker } = require('worker_threads');
  const w = new Worker(`
  const { parentPort, workerData } = require('worker_threads');

  const SPIN_MS = 1000;
  const start = Date.now();
  parentPort.on('message', (data) => {
    if (Date.now() - start < SPIN_MS) {
      parentPort.postMessage(data);
    } else {
      process.exit(0);
    }
  });
`, { eval: true });

  let count = 1;
  w.postMessage('hello\n');
  w.on('message', () => {
    count++;
    w.postMessage('hello\n');
  });

  w.on('exit', common.mustCall(() => {
    console.log(`parent posted ${count} messages`);
    files = fs.readdirSync(process.cwd());
    console.log('files found in working directory before worker exits', files);
    const workerProf = files.find((name) => name !== parentProf && name.endsWith('.log'));
    assert(workerProf, '--prof did not produce a profile log for worker thread');
    console.log('worker prof file:', workerProf);
  }));
} else {
  tmpdir.refresh();

  const workerProfRegexp = /worker prof file: (.+\.log)/;
  const parentProfRegexp = /parent prof file: (.+\.log)/;
  const { stdout } = spawnSyncAndAssert(
    process.execPath, ['--prof', __filename, 'child'],
    { cwd: tmpdir.path, encoding: 'utf8' }, {
      stdout(output) {
        console.log(output);
        assert.match(output, workerProfRegexp);
        assert.match(output, parentProfRegexp);
        return true;
      }
    });
  const workerProf = stdout.match(workerProfRegexp)[1];
  const parentProf = stdout.match(parentProfRegexp)[1];

  const logfiles = fs.readdirSync(tmpdir.path).filter((name) => name.endsWith('.log'));
  assert.deepStrictEqual(new Set(logfiles), new Set([workerProf, parentProf]));

  const workerLines = fs.readFileSync(tmpdir.resolve(workerProf), 'utf8').split('\n');
  const parentLines = fs.readFileSync(tmpdir.resolve(parentProf), 'utf8').split('\n');

  const workerTicks = workerLines.filter((line) => line.startsWith('tick'));
  const parentTicks = parentLines.filter((line) => line.startsWith('tick'));

  console.log('worker ticks', workerTicks.length);
  console.log('parent ticks', parentTicks.length);
  // When not tracking Worker threads, only 1 or 2 ticks would
  // have been recorded.
  // prof_sampling_interval is by default 1 millisecond. A higher NODE_TEST_SPIN_MS
  // should result in more ticks, while 15 should be safe on most machines.
  assert(workerTicks.length > 15, `worker ticks <= 15:\n${workerTicks.join('\n')}`);
  assert(parentTicks.length > 15, `parent ticks <= 15:\n${parentTicks.join('\n')}`);
}

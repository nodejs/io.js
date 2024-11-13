// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
// test-cluster-worker-kill.js
// verifies that, when a child process is killed (we use SIGKILL)
// - the primary receives the proper events in the proper order, no duplicates
// - the exitCode and signalCode are correct in the 'exit' event
// - the worker.exitedAfterDisconnect flag, and worker.state are correct
// - the worker process actually goes away

const common = require('../common');
const assert = require('assert');
const cluster = require('cluster');

if (cluster.isWorker) {
  const http = require('http');
  const server = http.Server(() => { });

  server.once('listening', common.mustCall());
  server.listen(0, '127.0.0.1');

} else if (cluster.isPrimary) {

  const KILL_SIGNAL = 'SIGKILL';
  const expected_results = {
    cluster_emitDisconnect: [1, "the cluster did not emit 'disconnect'"],
    cluster_emitExit: [1, "the cluster did not emit 'exit'"],
    cluster_exitCode: [null, 'the cluster exited w/ incorrect exitCode'],
    cluster_signalCode: [KILL_SIGNAL,
                         'the cluster exited w/ incorrect signalCode'],
    worker_emitDisconnect: [1, "the worker did not emit 'disconnect'"],
    worker_emitExit: [1, "the worker did not emit 'exit'"],
    worker_state: ['disconnected', 'the worker state is incorrect'],
    worker_exitedAfter: [false, 'the .exitedAfterDisconnect flag is incorrect'],
    worker_died: [true, 'the worker is still running'],
    worker_exitCode: [null, 'the worker exited w/ incorrect exitCode'],
    worker_signalCode: [KILL_SIGNAL,
                        'the worker exited w/ incorrect signalCode']
  };
  const results = {
    cluster_emitDisconnect: 0,
    cluster_emitExit: 0,
    worker_emitDisconnect: 0,
    worker_emitExit: 0
  };


  // start worker
  const worker = cluster.fork();

  // When the worker is up and running, kill it
  worker.once('listening', common.mustCall(() => {
    worker.process.kill(KILL_SIGNAL);
  }));


  // Check cluster events
  cluster.on('disconnect', common.mustCall(() => {
    results.cluster_emitDisconnect += 1;
  }));
  cluster.on('exit', common.mustCall((worker) => {
    results.cluster_exitCode = worker.process.exitCode;
    results.cluster_signalCode = worker.process.signalCode;
    results.cluster_emitExit += 1;
  }));

  // Check worker events and properties
  worker.on('disconnect', common.mustCall(() => {
    results.worker_emitDisconnect += 1;
    results.worker_exitedAfter = worker.exitedAfterDisconnect;
    results.worker_state = worker.state;
  }));

  // Check that the worker died
  worker.once('exit', common.mustCall((exitCode, signalCode) => {
    results.worker_exitCode = exitCode;
    results.worker_signalCode = signalCode;
    results.worker_emitExit += 1;
    results.worker_died = !common.isAlive(worker.process.pid);
  }));

  process.on('exit', () => {
    checkResults(expected_results, results);
  });
}

// Some helper functions ...

function checkResults(expected_results, results) {
  for (const k in expected_results) {
    const actual = results[k];
    const expected = expected_results[k];

    assert.strictEqual(
      actual, expected?.length ? expected[0] : expected,
      `${expected[1] || ''} [expected: ${expected[0]} / actual: ${actual}]`);
  }
}

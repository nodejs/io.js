'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { duplexPair } = require('stream');
const { parentPort, Worker } = require('worker_threads');

// This test ensures that workers can be terminated without error while
// stream activity is ongoing, in particular the C++ function
// ReportWritesToJSStreamListener::OnStreamAfterReqFinished.

const MAX_ITERATIONS = 5;
const MAX_THREADS = 6;

// Do not use isMainThread so that this test itself can be run inside a Worker.
if (!process.env.HAS_STARTED_WORKER) {
  process.env.HAS_STARTED_WORKER = 1;

  function spinWorker(iter) {
    const w = new Worker(__filename);
    w.on('message', common.mustCall((msg) => {
      assert.strictEqual(msg, 'terminate');
      w.terminate();
    }));

    w.on('exit', common.mustCall(() => {
      if (iter < MAX_ITERATIONS)
        spinWorker(++iter);
    }));
  }

  for (let i = 0; i < MAX_THREADS; i++) {
    spinWorker(0);
  }
} else {
  const server = http2.createServer();
  let i = 0;
  server.on('stream', (stream, headers) => {
    if (i === 1) {
      parentPort.postMessage('terminate');
    }
    i++;

    stream.end('');
  });

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    createConnection: () => clientSide,
  });

  function makeRequests() {
    for (let i = 0; i < 3; i++) {
      client.request().end();
    }
    setImmediate(makeRequests);
  }
  makeRequests();
}

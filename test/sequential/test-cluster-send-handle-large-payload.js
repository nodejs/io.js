'use strict';
const common = require('../common');

const assert = require('assert');
const cluster = require('cluster');
const net = require('net');

const payload = 'a'.repeat(800004);

if (cluster.isPrimary) {
  const server = net.createServer();

  server.on('connection', common.mustCall((socket) => { socket.unref(); }));

  const worker = cluster.fork();
  worker.on('message', common.mustCall(({ payload: received }, handle) => {
    assert.strictEqual(payload, received);
    assert(handle instanceof net.Socket);
    server.close();
    handle.destroy();
  }));

  server.listen(0, common.mustCall(() => {
    const port = server.address().port;
    const socket = new net.Socket();
    socket.connect(port, (err) => {
      assert.ifError(err);
      worker.send({ payload }, socket);
    });
  }));
} else {
  process.on('message', common.mustCall(({ payload: received }, handle) => {
    assert.strictEqual(payload, received);
    assert(handle instanceof net.Socket);

    // On macOS, the primary process might not receive a message if it is sent
    // to soon, and then subsequent messages are also sometimes not received.
    //
    // (Is this a bug or expected operating system behavior like the way a file
    // watcher is returned before it's actually watching the file system on
    // macOS?)
    //
    // Send a second message after a delay on macOS.
    //
    // Refs: https://github.com/nodejs/node/issues/14747
    if (common.isMacOS)
      setTimeout(() => { process.send({ payload }, handle); }, 1000);
    else
      process.send({ payload }, handle);

    // Prepare for a clean exit.
    process.channel.unref();
  }));
}

// In this benchmark, we connect a client to the server, and write
// as many bytes as we can in the specified time (default = 10s)
'use strict';

const common = require('../common.js');
const util = require('util');

// If there are --dur=N and --len=N args, then
// run the function with those settings.
// if not, then queue up a bunch of child processes.
const bench = common.createBenchmark(main, {
  len: [102400, 1024 * 64 * 16],
  type: ['utf', 'asc', 'buf'],
  dur: [5],
}, {
  test: { len: 1024 },
  flags: [ '--expose-internals', '--no-warnings' ],
});

function main({ dur, len, type }) {
  const {
    TCP,
    TCPConnectWrap,
    constants: TCPConstants,
  } = common.binding('tcp_wrap');
  const { WriteWrap } = common.binding('stream_wrap');
  const PORT = common.PORT;

  const serverHandle = new TCP(TCPConstants.SERVER);
  let err = serverHandle.bind('127.0.0.1', PORT);
  if (err)
    fail(err, 'bind');

  err = serverHandle.listen(511);
  if (err)
    fail(err, 'listen');

  serverHandle.onconnection = function(err, clientHandle) {
    if (err)
      fail(err, 'connect');

    // The meat of the benchmark is right here:
    bench.start();
    let bytes = 0;

    setTimeout(() => {
      // report in Gb/sec
      bench.end((bytes * 8) / (1024 * 1024 * 1024));
      process.exit(0);
    }, dur * 1000);

    clientHandle.onread = function(buffer) {
      // We're not expecting to ever get an EOF from the client.
      // Just lots of data forever.
      if (!buffer)
        fail('read');

      // Don't slice the buffer. The point of this is to isolate, not
      // simulate real traffic.
      bytes += buffer.byteLength;
    };

    clientHandle.readStart();
  };

  client(type, len);

  function fail(err, syscall) {
    throw util._errnoException(err, syscall);
  }

  function client(type, len) {
    let chunk;
    switch (type) {
      case 'buf':
        chunk = Buffer.alloc(len, 'x');
        break;
      case 'utf':
        chunk = 'ü'.repeat(len / 2);
        break;
      case 'asc':
        chunk = 'x'.repeat(len);
        break;
      default:
        throw new Error(`invalid type: ${type}`);
    }

    const clientHandle = new TCP(TCPConstants.SOCKET);
    const connectReq = new TCPConnectWrap();
    const err = clientHandle.connect(connectReq, '127.0.0.1', PORT);

    if (err)
      fail(err, 'connect');

    clientHandle.readStart();

    connectReq.oncomplete = function(err) {
      if (err)
        fail(err, 'connect');

      while (clientHandle.writeQueueSize === 0)
        write();
    };

    function write() {
      const writeReq = new WriteWrap();
      writeReq.oncomplete = afterWrite;
      let err;
      switch (type) {
        case 'buf':
          err = clientHandle.writeBuffer(writeReq, chunk);
          break;
        case 'utf':
          err = clientHandle.writeUtf8String(writeReq, chunk);
          break;
        case 'asc':
          err = clientHandle.writeAsciiString(writeReq, chunk);
          break;
      }

      if (err)
        fail(err, 'write');
    }

    function afterWrite(err, handle) {
      if (err)
        fail(err, 'write');

      while (clientHandle.writeQueueSize === 0)
        write();
    }
  }
}

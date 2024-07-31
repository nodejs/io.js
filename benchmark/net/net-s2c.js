// Test the speed of .pipe() with sockets
'use strict';

const common = require('../common.js');
const PORT = common.PORT;

const bench = common.createBenchmark(main, {
  sendchunklen: [256, 32 * 1024, 128 * 1024, 16 * 64 * 1024],
  type: ['utf', 'asc', 'buf'],
  recvbuflen: [0, 64 * 1024, 1024 * 1024],
  recvbufgenfn: ['true', 'false'],
  dur: [5],
}, {
  test: { sendchunklen: 256 },
});

let chunk;
let encoding;
let recvbuf;
let received = 0;

function main({ dur, sendchunklen, type, recvbuflen, recvbufgenfn }) {
  if (isFinite(recvbuflen) && recvbuflen > 0)
    recvbuf = Buffer.alloc(recvbuflen);

  switch (type) {
    case 'buf':
      chunk = Buffer.alloc(sendchunklen, 'x');
      break;
    case 'utf':
      encoding = 'utf8';
      chunk = 'ü'.repeat(sendchunklen / 2);
      break;
    case 'asc':
      encoding = 'ascii';
      chunk = 'x'.repeat(sendchunklen);
      break;
    default:
      throw new Error(`invalid type: ${type}`);
  }

  const reader = new Reader();
  let writer;
  let socketOpts;
  if (recvbuf === undefined) {
    writer = new Writer();
    socketOpts = { port: PORT };
  } else {
    let buffer = recvbuf;
    if (recvbufgenfn === 'true') {
      let bufidx = -1;
      const bufpool = [
        recvbuf,
        Buffer.from(recvbuf),
        Buffer.from(recvbuf),
      ];
      buffer = () => {
        bufidx = (bufidx + 1) % bufpool.length;
        return bufpool[bufidx];
      };
    }
    socketOpts = {
      port: PORT,
      onread: {
        buffer,
        callback: function(nread, buf) {
          received += nread;
        },
      },
    };
  }

  // The actual benchmark.
  const server = net.createServer((socket) => {
    reader.pipe(socket);
  });

  server.listen(PORT, () => {
    const socket = net.connect(socketOpts);
    socket.on('connect', () => {
      bench.start();

      if (recvbuf === undefined)
        socket.pipe(writer);

      setTimeout(() => {
        const bytes = received;
        const gbits = (bytes * 8) / (1024 * 1024 * 1024);
        bench.end(gbits);
        process.exit(0);
      }, dur * 1000);
    });
  });
}

const net = require('net');

function Writer() {
  this.writable = true;
}

Writer.prototype.write = function(chunk, encoding, cb) {
  received += chunk.length;

  if (typeof encoding === 'function')
    encoding();
  else if (typeof cb === 'function')
    cb();

  return true;
};

// Doesn't matter, never emits anything.
Writer.prototype.on = function() {};
Writer.prototype.once = function() {};
Writer.prototype.emit = function() {};
Writer.prototype.prependListener = function() {};


function flow() {
  const dest = this.dest;
  const res = dest.write(chunk, encoding);
  if (!res)
    dest.once('drain', this.flow);
  else
    process.nextTick(this.flow);
}

function Reader() {
  this.flow = flow.bind(this);
  this.readable = true;
}

Reader.prototype.pipe = function(dest) {
  this.dest = dest;
  this.flow();
  return dest;
};

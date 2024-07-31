// Test the speed of .pipe() with sockets
'use strict';

const common = require('../common.js');
const net = require('net');
const PORT = common.PORT;

const bench = common.createBenchmark(main, {
  len: [2, 64, 102400, 1024 * 64 * 16],
  type: ['utf', 'asc', 'buf'],
  dur: [5],
}, {
  test: { len: 1024 },
});

let chunk;
let encoding;

function main({ dur, len, type }) {
  switch (type) {
    case 'buf':
      chunk = Buffer.alloc(len, 'x');
      break;
    case 'utf':
      encoding = 'utf8';
      chunk = 'ü'.repeat(len / 2);
      break;
    case 'asc':
      encoding = 'ascii';
      chunk = 'x'.repeat(len);
      break;
    default:
      throw new Error(`invalid type: ${type}`);
  }

  const reader = new Reader();
  const writer = new Writer();

  // The actual benchmark.
  const server = net.createServer((socket) => {
    socket.pipe(socket);
  });

  server.listen(PORT, () => {
    const socket = net.connect(PORT);
    socket.on('connect', () => {
      bench.start();

      reader.pipe(socket);
      socket.pipe(writer);

      setTimeout(() => {
        // Multiply by 2 since we're sending it first one way
        // then back again.
        const bytes = writer.received * 2;
        const gbits = (bytes * 8) / (1024 * 1024 * 1024);
        bench.end(gbits);
        process.exit(0);
      }, dur * 1000);
    });
  });
}

function Writer() {
  this.received = 0;
  this.writable = true;
}

Writer.prototype.write = function(chunk, encoding, cb) {
  this.received += chunk.length;

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

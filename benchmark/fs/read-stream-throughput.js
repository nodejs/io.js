// Test the throughput of the fs.WriteStream class.
'use strict';

const common = require('../common.js');
const fs = require('fs');
const assert = require('assert');

const tmpdir = require('../../test/common/tmpdir');
tmpdir.refresh();
const filename = tmpdir.resolve(`.removeme-benchmark-garbage-${process.pid}`);

const bench = common.createBenchmark(main, {
  encodingType: ['buf', 'asc', 'utf'],
  filesize: [1000 * 1024],
  highWaterMark: [1024, 4096, 65535, 1024 * 1024],
  n: 1024,
});

function main(conf) {
  const { encodingType, highWaterMark, filesize } = conf;
  let { n } = conf;

  let encoding = '';
  switch (encodingType) {
    case 'buf':
      encoding = null;
      break;
    case 'asc':
      encoding = 'ascii';
      break;
    case 'utf':
      encoding = 'utf8';
      break;
    default:
      throw new Error(`invalid encodingType: ${encodingType}`);
  }

  // Make file
  const buf = Buffer.allocUnsafe(filesize);
  if (encoding === 'utf8') {
    // ü
    for (let i = 0; i < buf.length; i++) {
      buf[i] = i % 2 === 0 ? 0xC3 : 0xBC;
    }
  } else if (encoding === 'ascii') {
    buf.fill('a');
  } else {
    buf.fill('x');
  }

  try {
    fs.unlinkSync(filename);
  } catch {
    // Continue regardless of error.
  }
  const ws = fs.createWriteStream(filename);
  ws.on('close', runTest.bind(null, filesize, highWaterMark, encoding, n));
  ws.on('drain', write);
  write();
  function write() {
    do {
      n--;
    } while (false !== ws.write(buf) && n > 0);
    if (n === 0)
      ws.end();
  }
}

function runTest(filesize, highWaterMark, encoding, n) {
  assert.strictEqual(fs.statSync(filename).size, filesize * n);
  const rs = fs.createReadStream(filename, {
    highWaterMark,
    encoding,
  });

  rs.on('open', () => {
    bench.start();
  });

  let bytes = 0;
  rs.on('data', (chunk) => {
    bytes += chunk.length;
  });

  rs.on('end', () => {
    try {
      fs.unlinkSync(filename);
    } catch {
      // Continue regardless of error.
    }
    // MB/sec
    bench.end(bytes / (1024 * 1024));
  });
}

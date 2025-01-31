'use strict';
// Run this program with valgrind or efence with --expose_gc to expose the
// problem.

// Flags: --expose_gc

require('../common');
const assert = require('assert');
const { HTTPParser } = require('_http_common');

const kOnHeaders = HTTPParser.kOnHeaders | 0;
const kOnHeadersComplete = HTTPParser.kOnHeadersComplete | 0;
const kOnBody = HTTPParser.kOnBody | 0;
const kOnMessageComplete = HTTPParser.kOnMessageComplete | 0;

let headersComplete = 0;
let messagesComplete = 0;

function flushPool() {
  Buffer.allocUnsafe(Buffer.poolSize - 1);
  globalThis.gc();
}

function demoBug(part1, part2) {
  flushPool();

  const parser = new HTTPParser();
  parser.initialize(HTTPParser.REQUEST, {});

  parser.headers = [];
  parser.url = '';

  parser[kOnHeaders] = function(headers, url) {
    parser.headers = parser.headers.concat(headers);
    parser.url += url;
  };

  parser[kOnHeadersComplete] = function(info) {
    headersComplete++;
    console.log('url', info.url);
  };

  parser[kOnBody] = () => {};

  parser[kOnMessageComplete] = function() {
    messagesComplete++;
  };


  // We use a function to eliminate references to the Buffer b
  // We want b to be GCed. The parser will hold a bad reference to it.
  (function() {
    const b = Buffer.from(part1);
    flushPool();

    console.log('parse the first part of the message');
    parser.execute(b, 0, b.length);
  })();

  flushPool();

  (function() {
    const b = Buffer.from(part2);

    console.log('parse the second part of the message');
    parser.execute(b, 0, b.length);
    parser.finish();
  })();

  flushPool();
}


demoBug('POST /1', '/22 HTTP/1.1\r\n' +
        'Content-Type: text/plain\r\n' +
        'Content-Length: 4\r\n\r\n' +
        'pong');

demoBug('POST /1/22 HTTP/1.1\r\n' +
        'Content-Type: tex', 't/plain\r\n' +
        'Content-Length: 4\r\n\r\n' +
        'pong');

process.on('exit', function() {
  assert.strictEqual(headersComplete, 2);
  assert.strictEqual(messagesComplete, 2);
  console.log('done!');
});

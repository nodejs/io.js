'use strict';

// Verifies that a full HTTP2 pipeline handles backpressure.

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { duplexPair } = require('stream');

{
  let req;
  const server = http2.createServer();
  server.on('stream', mustCallAsync(async (stream, headers) => {
    stream.respond({
      'content-type': 'text/html',
      ':status': 200
    });
    req._readableState.highWaterMark = 20;
    stream._writableState.highWaterMark = 20;
    assert.strictEqual(stream.write('A'.repeat(5)), true);
    assert.strictEqual(stream.write('A'.repeat(40)), false);
    assert.strictEqual(await event(req, 'data'), 'A'.repeat(5));
    assert.strictEqual(await event(req, 'data'), 'A'.repeat(40));
    await event(stream, 'drain');
    assert.strictEqual(stream.write('A'.repeat(5)), true);
    assert.strictEqual(stream.write('A'.repeat(40)), false);
  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    createConnection: common.mustCall(() => clientSide)
  });

  req = client.request({ ':path': '/' });
  req.setEncoding('utf8');
  req.end();
}

function event(ee, eventName) {
  return new Promise((resolve) => {
    ee.once(eventName, common.mustCall(resolve));
  });
}

function mustCallAsync(fn, exact) {
  return common.mustCall((...args) => {
    return Promise.resolve(fn(...args)).then(common.mustCall((val) => val));
  }, exact);
}

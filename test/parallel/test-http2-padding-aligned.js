'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
const assert = require('assert');
const http2 = require('http2');
const { PADDING_STRATEGY_ALIGNED, PADDING_STRATEGY_CALLBACK } = http2.constants;
const { duplexPair } = require('stream');

{
  const testData = '<h1>Hello World.</h1>';
  const server = http2.createServer({
    paddingStrategy: PADDING_STRATEGY_ALIGNED
  });
  server.on('stream', common.mustCall((stream, headers) => {
    stream.respond({
      'content-type': 'text/html',
      ':status': 200
    });
    stream.end(testData);
  }));

  const [ clientSide, serverSide ] = duplexPair();

  // The lengths of the expected writes... note that this is highly
  // sensitive to how the internals are implemented.
  const serverLengths = [24, 9, 9, 32];
  const clientLengths = [9, 9, 48, 9, 1, 21, 1];

  // Adjust for the 24-byte preamble and two 9-byte settings frames, and
  // the result must be equally divisible by 8
  assert.strictEqual(
    (serverLengths.reduce((i, n) => i + n) - 24 - 9 - 9) % 8, 0);

  // Adjust for two 9-byte settings frames, and the result must be equally
  // divisible by 8
  assert.strictEqual(
    (clientLengths.reduce((i, n) => i + n) - 9 - 9) % 8, 0);

  serverSide.on('data', common.mustCall((chunk) => {
    assert.strictEqual(chunk.length, serverLengths.shift());
  }, serverLengths.length));
  clientSide.on('data', common.mustCall((chunk) => {
    assert.strictEqual(chunk.length, clientLengths.shift());
  }, clientLengths.length));

  server.emit('connection', serverSide);

  const client = http2.connect('http://localhost:80', {
    paddingStrategy: PADDING_STRATEGY_ALIGNED,
    createConnection: common.mustCall(() => clientSide)
  });

  const req = client.request({ ':path': '/a' });

  req.on('response', common.mustCall());

  req.setEncoding('utf8');
  req.on('data', common.mustCall((data) => {
    assert.strictEqual(data, testData);
  }));
  req.on('close', common.mustCall(() => {
    clientSide.destroy();
    clientSide.end();
  }));
  req.end();
}

// PADDING_STRATEGY_CALLBACK has been aliased to mean aligned padding.
assert.strictEqual(PADDING_STRATEGY_ALIGNED, PADDING_STRATEGY_CALLBACK);

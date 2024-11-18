// Flags: --expose-internals
'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const { kHighWaterMark } = require('_http_outgoing');

const { setDefaultHighWaterMark } = require('internal/streams/state');

function listen(server) {
  server.listen(0, common.mustCall(() => {
    http.get({
      port: server.address().port,
    }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

// Test `options.highWaterMark` fails if less than zero.
{
  assert.throws(() => {
    http.createServer({
      highWaterMark: -1,
    });
  }, { code: 'ERR_OUT_OF_RANGE' });
}

// Test socket watermark is set with the value in `options.highWaterMark`.
{
  const waterMarkValue = 17000;
  const serverWaterMarkValue = 14000;
  const server = http.createServer({
    highWaterMark: serverWaterMarkValue,
  }, common.mustCall((req, res) => {
    assert.strictEqual(req._readableState.highWaterMark, serverWaterMarkValue);
    assert.strictEqual(res[kHighWaterMark], serverWaterMarkValue);
    res.statusCode = 200;
    res.end();
  }));

  setDefaultHighWaterMark(false, waterMarkValue);
  listen(server);
}

// Test socket watermark is the default if `highWaterMark` in `options` is unset.
{
  const waterMarkValue = 13000;
  const server = http.createServer(
    common.mustCall((req, res) => {
      assert.strictEqual(req._readableState.highWaterMark, waterMarkValue);
      assert.strictEqual(res[kHighWaterMark], waterMarkValue);
      res.statusCode = 200;
      res.end();
    })
  );

  setDefaultHighWaterMark(false, waterMarkValue);
  listen(server);
}

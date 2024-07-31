'use strict';

// Test https highWaterMark

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const https = require('https');
const fixtures = require('../common/fixtures');

let counter = 0;

function loadCallback(highWaterMark) {
  return common.mustCall(function(res) {
    assert.strictEqual(highWaterMark, res.readableHighWaterMark);
    counter--;
    console.log('back from https request. ',
                `highWaterMark = ${res.readableHighWaterMark}`);
    if (counter === 0) {
      httpsServer.close();
      console.log('ok');
    }
    res.resume();
  });
}

// create server
const httpsServer = https.createServer({
  key: fixtures.readKey('agent1-key.pem'),
  cert: fixtures.readKey('agent1-cert.pem')
}, common.mustCall(function(req, res) {
  res.writeHead(200, {});
  res.end('ok');
}, 3)).listen(0, common.mustCall(function(err) {
  console.log(`test https server listening on port ${this.address().port}`);
  assert.ifError(err);

  https.request({
    method: 'GET',
    path: `/${counter++}`,
    host: 'localhost',
    port: this.address().port,
    rejectUnauthorized: false,
    highWaterMark: 128000,
  }, loadCallback(128000)).on('error', common.mustNotCall()).end();

  https.request({
    method: 'GET',
    path: `/${counter++}`,
    host: 'localhost',
    port: this.address().port,
    rejectUnauthorized: false,
    highWaterMark: 0,
  }, loadCallback(0)).on('error', common.mustNotCall()).end();

  https.request({
    method: 'GET',
    path: `/${counter++}`,
    host: 'localhost',
    port: this.address().port,
    rejectUnauthorized: false,
    highWaterMark: undefined,
  }, loadCallback(process.platform === 'win32' ? 16 * 1024 : 64 * 1024)).on('error', common.mustNotCall()).end();
}));

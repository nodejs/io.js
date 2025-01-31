'use strict';
const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { hasOpenSSL } = require('../common/crypto');

const assert = require('assert');
const https = require('https');
const net = require('net');

const server = net.createServer(function(s) {
  s.once('data', function() {
    s.end('I was waiting for you, hello!', function() {
      s.destroy();
    });
  });
});

server.listen(0, function() {
  const req = https.request({ port: this.address().port });
  req.end();

  let expectedErrorMessage = new RegExp('wrong version number');
  if (hasOpenSSL(3, 2)) {
    expectedErrorMessage = new RegExp('packet length too long');
  };
  req.once('error', common.mustCall(function(err) {
    assert(expectedErrorMessage.test(err.message));
    server.close();
  }));
});

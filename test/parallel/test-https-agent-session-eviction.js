// Flags: --tls-min-v1.0
'use strict';

const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { readKey } = require('../common/fixtures');
const { hasOpenSSL } = require('../common/crypto');

const https = require('https');
const { SSL_OP_NO_TICKET } = require('crypto').constants;

const options = {
  key: readKey('agent1-key.pem'),
  cert: readKey('agent1-cert.pem'),
  secureOptions: SSL_OP_NO_TICKET,
  ciphers: 'RSA@SECLEVEL=0'
};

// Create TLS1.2 server
https.createServer(options, function(req, res) {
  res.writeHead(200, { 'Connection': 'close' });
  res.end('ohai');
}).listen(0, function() {
  first(this);
});

// Do request and let agent cache the session
function first(server) {
  const port = server.address().port;
  const req = https.request({
    port: port,
    rejectUnauthorized: false
  }, function(res) {
    res.resume();

    server.close(function() {
      faultyServer(port);
    });
  });
  req.end();
}

// Create TLS1 server
function faultyServer(port) {
  options.secureProtocol = 'TLSv1_method';
  https.createServer(options, function(req, res) {
    res.writeHead(200, { 'Connection': 'close' });
    res.end('hello faulty');
  }).listen(port, function() {
    second(this);
  });
}

// Attempt to request using cached session
function second(server, session) {
  const req = https.request({
    port: server.address().port,
    ciphers: (hasOpenSSL(3, 1) ? 'DEFAULT:@SECLEVEL=0' : 'DEFAULT'),
    rejectUnauthorized: false
  }, function(res) {
    res.resume();
  });

  // Although we have a TLS 1.2 session to offer to the TLS 1.0 server,
  // connection to the TLS 1.0 server should work.
  req.on('response', common.mustCall(function(res) {
    // The test is now complete for OpenSSL 1.1.0.
    server.close();
  }));

  req.end();
}

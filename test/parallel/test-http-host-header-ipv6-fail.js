'use strict';

// When using the object form of http.request and using an IPv6 address
// as a hostname, and using a non-standard port, the Host header
// is improperly formatted.
// Issue: https://github.com/nodejs/node/issues/5308
// As per https://tools.ietf.org/html/rfc7230#section-5.4 and
// https://tools.ietf.org/html/rfc3986#section-3.2.2
// the IPv6 address should be enclosed in square brackets

const common = require('../common');
const assert = require('assert');
const http = require('http');
const net = require('net');

const requests = [
  { host: 'foo:1234', headers: { expectedhost: 'foo:1234:80' } },
  { host: '::1', headers: { expectedhost: '[::1]:80' } },
];

function createLocalConnection(options) {
  options.host = undefined;
  options.port = this.port;
  options.path = undefined;
  return net.createConnection(options);
}

http.createServer(common.mustCall(function(req, res) {
  this.requests ||= 0;
  assert.strictEqual(req.headers.host, req.headers.expectedhost);
  res.end();
  if (++this.requests === requests.length)
    this.close();
}, requests.length)).listen(0, function() {
  const address = this.address();
  for (let i = 0; i < requests.length; ++i) {
    requests[i].createConnection =
      common.mustCall(createLocalConnection.bind(address));
    http.get(requests[i]);
  }
});

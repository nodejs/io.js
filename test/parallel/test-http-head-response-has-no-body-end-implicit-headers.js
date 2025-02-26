'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');

// This test is to make sure that when the HTTP server
// responds to a HEAD request with data to res.end,
// it does not send any body but the response is sent
// anyway.

const server = http.createServer(function(req, res) {
  res.end('FAIL'); // broken: sends FAIL from hot path.
});
server.listen(0);

server.on('listening', common.mustCall(function() {
  const req = http.request({
    port: this.address().port,
    method: 'HEAD',
    path: '/'
  }, common.mustCall(function(res) {
    assert.strictEqual(
      res.headers['content-length'],
      '4',
      new Error('Expected Content-Length header to be of length 4')
    );

    res.on('end', common.mustCall(function() {
      server.close();
    }));
    res.resume();
  }));
  req.end();
}));

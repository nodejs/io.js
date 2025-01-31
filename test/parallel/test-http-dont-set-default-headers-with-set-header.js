'use strict';

const common = require('../common');
const assert = require('assert');
const http = require('http');

const server = http.createServer(common.mustCall(function(req, res) {
  assert.deepStrictEqual(req.rawHeaders, [
    'test', 'value',
    'HOST', `127.0.0.1:${server.address().port}`,
    'foo', 'bar',
    'foo', 'baz',
    'connection', 'close',
  ]);

  res.end('ok');
  server.close();
}));
server.listen(0, common.localhostIPv4, function() {
  const req = http.request({
    method: 'POST',
    host: common.localhostIPv4,
    port: this.address().port,
    setDefaultHeaders: false,
  });

  req.setHeader('test', 'value');
  req.setHeader('HOST', `${common.localhostIPv4}:${server.address().port}`);
  req.setHeader('foo', ['bar', 'baz']);
  req.setHeader('connection', 'close');

  req.end();
});

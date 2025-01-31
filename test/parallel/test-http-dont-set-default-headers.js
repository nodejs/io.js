'use strict';

const common = require('../common');
const assert = require('assert');
const http = require('http');

const server = http.createServer(common.mustCall(function(req, res) {
  assert.deepStrictEqual(req.rawHeaders, [
    'host', `${common.localhostIPv4}:${server.address().port}`,
    'foo', 'bar',
    'test', 'value',
    'foo', 'baz',
  ]);

  res.end('ok');
  server.close();
}));
server.listen(0, common.localhostIPv4, function() {
  http.request({
    method: 'POST',
    host: common.localhostIPv4,
    port: this.address().port,
    setDefaultHeaders: false,
    headers: [
      'host', `${common.localhostIPv4}:${server.address().port}`,
      'foo', 'bar',
      'test', 'value',
      'foo', 'baz',
    ]
  }).end();
});

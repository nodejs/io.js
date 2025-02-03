'use strict';

const common = require('../common');
const { strictEqual } = require('assert');
const { createServer, request } = require('http');

const server = createServer(common.mustCall((req, res) => {
  strictEqual(req.method, 'QUERY');
  res.end('OK');
}));

server.listen(0, common.mustCall(() => {
  const req = request({ port: server.address().port, method: 'QUERY' }, common.mustCall((res) => {
    strictEqual(res.statusCode, 200);

    let buffer = '';
    res.setEncoding('utf-8');

    res.on('data', (c) => buffer += c);
    res.on('end', common.mustCall(() => {
      strictEqual(buffer, 'OK');
      server.close();
    }));
  }));

  req.end();
}));

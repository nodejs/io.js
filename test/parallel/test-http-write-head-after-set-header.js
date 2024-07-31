'use strict';

const common = require('../common');
const Countdown = require('../common/countdown');
const assert = require('assert');
const { createServer, request } = require('http');

const server = createServer(common.mustCall((req, res) => {
  if (req.url.includes('setHeader')) {
    res.setHeader('set-val', 'abc');
  }

  res.writeHead(200, [
    'array-val', '1',
    'array-val', '2',
  ]);

  res.end();
}, 2));

const countdown = new Countdown(2, () => server.close());

server.listen(0, common.mustCall(() => {
  request({
    port: server.address().port
  }, common.mustCall((res) => {
    assert.deepStrictEqual(res.rawHeaders.slice(0, 4), [
      'array-val', '1',
      'array-val', '2',
    ]);

    countdown.dec();
  })).end();

  request({
    port: server.address().port,
    path: '/?setHeader'
  }, common.mustCall((res) => {
    assert.deepStrictEqual(res.rawHeaders.slice(0, 6), [
      'set-val', 'abc',
      'array-val', '1',
      'array-val', '2',
    ]);

    countdown.dec();
  })).end();
}));

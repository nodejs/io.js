'use strict';
require('../common');
const assert = require('assert');
const http = require('http');
const Countdown = require('../common/countdown');

const expectedHeadersMultipleWrites = {
  'connection': 'keep-alive',
  'transfer-encoding': 'chunked',
};

const expectedHeadersEndWithData = {
  'connection': 'keep-alive',
  'content-length': String('hello world'.length),
};

const expectedHeadersEndNoData = {
  'connection': 'keep-alive',
  'content-length': '0',
};


const countdown = new Countdown(3, () => server.close());

const server = http.createServer(function(req, res) {
  res.removeHeader('Date');
  res.setHeader('Keep-Alive', 'timeout=1');

  switch (req.url.slice(1)) {
    case 'multiple-writes':
      delete req.headers.host;
      assert.deepStrictEqual(req.headers, expectedHeadersMultipleWrites);
      res.write('hello');
      res.end('world');
      break;
    case 'end-with-data':
      delete req.headers.host;
      assert.deepStrictEqual(req.headers, expectedHeadersEndWithData);
      res.end('hello world');
      break;
    case 'empty':
      delete req.headers.host;
      assert.deepStrictEqual(req.headers, expectedHeadersEndNoData);
      res.end();
      break;
    default:
      throw new Error('Unreachable');
  }

  countdown.dec();
});

server.listen(0, function() {
  let req;

  req = http.request({
    port: this.address().port,
    method: 'POST',
    path: '/multiple-writes'
  });
  req.removeHeader('Date');
  req.write('hello ');
  req.end('world');
  req.on('response', function(res) {
    assert.deepStrictEqual(res.headers, { ...expectedHeadersMultipleWrites, 'keep-alive': 'timeout=1' });
    res.resume();
  });

  req = http.request({
    port: this.address().port,
    method: 'POST',
    path: '/end-with-data'
  });
  req.removeHeader('Date');
  req.end('hello world');
  req.on('response', function(res) {
    assert.deepStrictEqual(res.headers, { ...expectedHeadersEndWithData, 'keep-alive': 'timeout=1' });
    res.resume();
  });

  req = http.request({
    port: this.address().port,
    method: 'POST',
    path: '/empty'
  });
  req.removeHeader('Date');
  req.end();
  req.on('response', function(res) {
    assert.deepStrictEqual(res.headers, { ...expectedHeadersEndNoData, 'keep-alive': 'timeout=1' });
    res.resume();
  });

});

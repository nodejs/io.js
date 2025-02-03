'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');
const { duplexPair } = require('stream');

// Test 1: Simple HTTP test, no keep-alive.
{
  const testData = 'Hello, World!\n';
  const server = http.createServer(common.mustCall((req, res) => {
    res.statusCode = 200;
    res.setHeader('Content-Type', 'text/plain');
    res.end(testData);
  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  const req = http.request({
    createConnection: common.mustCall(() => clientSide)
  }, common.mustCall((res) => {
    res.setEncoding('utf8');
    res.on('data', common.mustCall((data) => {
      assert.strictEqual(data, testData);
    }));
    res.on('end', common.mustCall());
  }));
  req.end();
}

// Test 2: Keep-alive for 2 requests.
{
  const testData = 'Hello, World!\n';
  const server = http.createServer(common.mustCall((req, res) => {
    res.statusCode = 200;
    res.setHeader('Content-Type', 'text/plain');
    res.end(testData);
  }, 2));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  function doRequest(cb) {
    const req = http.request({
      createConnection: common.mustCall(() => clientSide),
      headers: { Connection: 'keep-alive' }
    }, common.mustCall((res) => {
      res.setEncoding('utf8');
      res.on('data', common.mustCall((data) => {
        assert.strictEqual(data, testData);
      }));
      res.on('end', common.mustCall(cb));
    }));
    req.shouldKeepAlive = true;
    req.end();
  }

  doRequest(() => {
    doRequest();
  });
}

// Test 3: Connection: close request/response with chunked
{
  const testData = 'Hello, World!\n';
  const server = http.createServer(common.mustCall((req, res) => {
    req.setEncoding('utf8');
    req.resume();
    req.on('data', common.mustCall(function test3_req_data(data) {
      assert.strictEqual(data, testData);
    }));
    req.once('end', function() {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'text/plain');
      res.write(testData);
      res.end();
    });
  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);
  clientSide.on('end', common.mustCall());
  serverSide.on('end', common.mustCall());

  const req = http.request({
    createConnection: common.mustCall(() => clientSide),
    method: 'PUT',
    headers: { 'Connection': 'close' }
  }, common.mustCall((res) => {
    res.setEncoding('utf8');
    res.on('data', common.mustCall(function test3_res_data(data) {
      assert.strictEqual(data, testData);
    }));
    res.on('end', common.mustCall());
  }));
  req.write(testData);
  req.end();
}

// Test 4: Connection: close request/response with Content-Length
// The same as Test 3, but with Content-Length headers
{
  const testData = 'Hello, World!\n';
  const server = http.createServer(common.mustCall((req, res) => {
    assert.strictEqual(req.headers['content-length'], testData.length + '');
    req.setEncoding('utf8');
    req.on('data', common.mustCall(function test4_req_data(data) {
      assert.strictEqual(data, testData);
    }));
    req.once('end', function() {
      res.statusCode = 200;
      res.setHeader('Content-Type', 'text/plain');
      res.setHeader('Content-Length', testData.length);
      res.write(testData);
      res.end();
    });

  }));

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);
  clientSide.on('end', common.mustCall());
  serverSide.on('end', common.mustCall());

  const req = http.request({
    createConnection: common.mustCall(() => clientSide),
    method: 'PUT',
    headers: { 'Connection': 'close' }
  }, common.mustCall((res) => {
    res.setEncoding('utf8');
    assert.strictEqual(res.headers['content-length'], testData.length + '');
    res.on('data', common.mustCall(function test4_res_data(data) {
      assert.strictEqual(data, testData);
    }));
    res.on('end', common.mustCall());
  }));
  req.setHeader('Content-Length', testData.length);
  req.write(testData);
  req.end();
}

// Test 5: The client sends garbage.
{
  const server = http.createServer(common.mustNotCall());

  const [ clientSide, serverSide ] = duplexPair();
  server.emit('connection', serverSide);

  server.on('clientError', common.mustCall());

  // Send something that is not an HTTP request.
  clientSide.end(
    'I’m reading a book about anti-gravity. It’s impossible to put down!');
}

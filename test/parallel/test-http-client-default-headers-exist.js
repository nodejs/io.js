// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');

const { once } = require('events');

const expectedHeaders = {
  'DELETE': ['host', 'connection'],
  'GET': ['host', 'connection'],
  'HEAD': ['host', 'connection'],
  'OPTIONS': ['host', 'connection'],
  'POST': ['host', 'connection', 'content-length'],
  'PUT': ['host', 'connection', 'content-length'],
  'TRACE': ['host', 'connection']
};

const expectedMethods = Object.keys(expectedHeaders);

const server = http.createServer(common.mustCall((req, res) => {
  res.end();

  assert(Object.hasOwn(expectedHeaders, req.method),
         `${req.method} was an unexpected method`);

  const requestHeaders = Object.keys(req.headers);
  for (const header of requestHeaders) {
    assert.ok(
      expectedHeaders[req.method].includes(header.toLowerCase()),
      `${header} should not exist for method ${req.method}`
    );
  }

  assert.strictEqual(
    requestHeaders.length,
    expectedHeaders[req.method].length,
    `some headers were missing for method: ${req.method}`
  );
}, expectedMethods.length));

server.listen(0, common.mustCall(() => {
  Promise.all(expectedMethods.map(async (method) => {
    const request = http.request({
      method: method,
      port: server.address().port
    }).end();
    return once(request, 'response');
  })).then(common.mustCall(() => { server.close(); }));
}));

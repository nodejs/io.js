'use strict';
const common = require('../common');
const http = require('http');
const assert = require('assert');

const nonUtf8Header = 'bår';
const nonUtf8ToLatin1 = Buffer.from(nonUtf8Header).toString('latin1');

{
  const server = http.createServer(common.mustCall((req, res) => {
    res.writeHead(200, [
      'content-disposition',
      Buffer.from(nonUtf8Header).toString('binary'),
    ]);
    res.end('hello');
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers['content-disposition'], nonUtf8ToLatin1);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  // Test multi-value header
  const server = http.createServer(common.mustCall((req, res) => {
    res.writeHead(200, [
      'content-disposition',
      [Buffer.from(nonUtf8Header).toString('binary')],
    ]);
    res.end('hello');
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers['content-disposition'], nonUtf8ToLatin1);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

{
  const server = http.createServer(common.mustCall((req, res) => {
    res.writeHead(200, [
      'Content-Length', '5',
      'content-disposition',
      Buffer.from(nonUtf8Header).toString('binary'),
    ]);
    res.end('hello');
  }));

  server.listen(0, common.mustCall(() => {
    http.get({ port: server.address().port }, (res) => {
      assert.strictEqual(res.statusCode, 200);
      assert.strictEqual(res.headers['content-disposition'], nonUtf8ToLatin1);
      res.resume().on('end', common.mustCall(() => {
        server.close();
      }));
    });
  }));
}

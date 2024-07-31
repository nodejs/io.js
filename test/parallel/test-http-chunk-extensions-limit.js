'use strict';

const common = require('../common');
const http = require('http');
const net = require('net');
const assert = require('assert');

// The maximum http chunk extension size is set in `src/node_http_parser.cc`.
// These tests assert that once the extension size is reached, an HTTP 413
// response is returned.
// Currently, the max size is set to 16KiB (16384).

// Verify that chunk extensions are limited in size when sent all together.
{
  const server = http.createServer((req, res) => {
    req.on('end', () => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('bye');
    });

    req.resume();
  });

  server.listen(0, () => {
    const port = server.address().port;
    const sock = net.connect(port);
    let data = '';

    sock.on('data', (chunk) => data += chunk.toString('utf-8'));

    sock.on('end', common.mustCall(function() {
      assert.strictEqual(data, 'HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n');
      server.close();
    }));

    sock.end('' +
      'GET / HTTP/1.1\r\n' +
      `Host: localhost:${port}\r\n` +
      'Transfer-Encoding: chunked\r\n\r\n' +
      '2;' + 'a'.repeat(17000) + '\r\n' + // Chunk size + chunk ext + CRLF
      'AA\r\n' + // Chunk data
      '0\r\n' + // Last chunk
      '\r\n' // End of http message
    );
  });
}

// Verify that chunk extensions are limited in size when sent in parts
{
  const server = http.createServer((req, res) => {
    req.on('end', () => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('bye');
    });

    req.resume();
  });

  server.listen(0, () => {
    const port = server.address().port;
    const sock = net.connect(port);
    let data = '';

    sock.on('data', (chunk) => data += chunk.toString('utf-8'));

    sock.on('end', common.mustCall(function() {
      assert.strictEqual(data, 'HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n');
      server.close();
    }));

    sock.write('' +
    'GET / HTTP/1.1\r\n' +
    `Host: localhost:${port}\r\n` +
    'Transfer-Encoding: chunked\r\n\r\n' +
    '2;' // Chunk size + start of chunk-extension
    );

    sock.write('A'.repeat(8500)); // Write half of the chunk-extension

    queueMicrotask(() => {
      sock.write('A'.repeat(8500) + '\r\n' + // Remaining half of the chunk-extension
      'AA\r\n' + // Chunk data
      '0\r\n' + // Last chunk
      '\r\n' // End of http message
      );
    });
  });
}

// Verify the chunk extensions is correctly reset after a chunk
{
  const server = http.createServer((req, res) => {
    req.on('end', () => {
      res.writeHead(200, { 'content-type': 'text/plain', 'connection': 'close', 'date': 'now' });
      res.end('bye');
    });

    req.resume();
  });

  server.listen(0, () => {
    const port = server.address().port;
    const sock = net.connect(port);
    let data = '';

    sock.on('data', (chunk) => data += chunk.toString('utf-8'));

    sock.on('end', common.mustCall(function() {
      assert.strictEqual(
        data,
        'HTTP/1.1 200 OK\r\n' +
        'content-type: text/plain\r\n' +
        'connection: close\r\n' +
        'date: now\r\n' +
        'Transfer-Encoding: chunked\r\n' +
        '\r\n' +
        '3\r\n' +
        'bye\r\n' +
        '0\r\n' +
        '\r\n',
      );

      server.close();
    }));

    sock.end('' +
      'GET / HTTP/1.1\r\n' +
      `Host: localhost:${port}\r\n` +
      'Transfer-Encoding: chunked\r\n\r\n' +
      '2;' + 'A'.repeat(10000) + '=bar\r\nAA\r\n' +
      '2;' + 'A'.repeat(10000) + '=bar\r\nAA\r\n' +
      '2;' + 'A'.repeat(10000) + '=bar\r\nAA\r\n' +
      '0\r\n\r\n'
    );
  });
}

'use strict';
const common = require('../common');
const http = require('http');
const net = require('net');
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();

let count = 0;
let server1;
let server2;

function request(options) {
  count++;
  http.get({
    ...options,
    createConnection: (...args) => {
      return net.connect(...args);
    }
  }, (res) => {
    res.resume();
    res.on('end', () => {
      if (--count === 0) {
        server1.close();
        server2.close();
      }
    });
  });
}

server1 = http.createServer((req, res) => {
  res.end('ok');
}).listen(common.PIPE, () => {
  server2 = http.createServer((req, res) => {
    res.end('ok');
  }).listen(() => {
    request({
      path: '/',
      socketPath: common.PIPE,
    });

    request({
      socketPath: common.PIPE,
    });

    request({
      path: '/',
      port: server2.address().port,
    });

    request({
      port: server2.address().port,
    });
  });
});

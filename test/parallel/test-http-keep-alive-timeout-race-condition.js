'use strict';

const common = require('../common');
const http = require('http');

const makeRequest = (port, agent) =>
  new Promise((resolve, reject) => {
    const req = http.get(
      { path: '/', port, agent },
      (res) => {
        res.resume();
        res.on('end', () => resolve());
      },
    );
    req.on('error', (e) => reject(e));
    req.end();
  });

const server = http.createServer(
  { keepAliveTimeout: common.platformTimeout(2000), keepAlive: true },
  common.mustCall((req, res) => {
    const body = 'hello world\n';
    res.writeHead(200, { 'Content-Length': body.length });
    res.write(body);
    res.end();
  }, 2)
);

const agent = new http.Agent({ maxSockets: 5, keepAlive: true });

server.listen(0, common.mustCall(async function() {
  await makeRequest(this.address().port, agent);
  // Block the event loop for 2 seconds
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 2000);
  await makeRequest(this.address().port, agent);
  server.close();
  agent.destroy();
}));

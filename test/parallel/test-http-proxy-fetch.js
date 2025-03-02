'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');
const { spawn } = require('child_process');
const http = require('http');
const { onConnect } = require('../fixtures/proxy-handler');

// Start a server to process the final request.
const server = http.createServer((req, res) => {
  res.end('Hello world');
});
server.on('error', (err) => { console.log('Server error', err); });

server.listen(0, common.mustCall(() => {
  // Start a proxy server to tunnel the request.
  const proxy = http.createServer();
  // If the request does not go through the proxy server, common.mustCall fails.
  proxy.on('connect', common.mustCall((req, clientSocket, head) => {
    console.log('Proxying CONNECT', req.url, req.headers);
    assert.strictEqual(req.url, `localhost:${server.address().port}`);
    onConnect(req, clientSocket, head);
  }));
  proxy.on('error', (err) => { console.log('Proxy error', err); });

  proxy.listen(0, common.mustCall(() => {
    const proxyAddress = `http://localhost:${proxy.address().port}`;
    const serverAddress = `http://localhost:${server.address().port}`;
    const child = spawn(process.execPath,
                        [fixtures.path('fetch-and-log.mjs')],
                        {
                          env: {
                            ...process.env,
                            HTTP_PROXY: proxyAddress,
                            NODE_USE_ENV_PROXY: true,
                            SERVER_ADDRESS: serverAddress,
                          },
                        });

    const stderr = [];
    const stdout = [];
    child.stderr.on('data', (chunk) => {
      stderr.push(chunk);
    });
    child.stdout.on('data', (chunk) => {
      stdout.push(chunk);
    });

    child.on('exit', common.mustCall(function(code, signal) {
      proxy.close();
      server.close();

      console.log('--- stderr ---');
      console.log(Buffer.concat(stderr).toString());
      console.log('--- stdout ---');
      const stdoutStr = Buffer.concat(stdout).toString();
      console.log(stdoutStr);
      assert.strictEqual(stdoutStr.trim(), 'Hello world');
      assert.strictEqual(code, 0);
      assert.strictEqual(signal, null);
    }));
  }));
}));

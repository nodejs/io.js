'use strict';
// Flags: --expose-gc
// just like test-gc-http-client.js,
// but aborting every connection that comes in.

const common = require('../common');
const { onGC } = require('../common/gc');
const http = require('http');
const os = require('os');

const cpus = os.availableParallelism();
let createClients = true;
let done = 0;
let count = 0;
let countGC = 0;

function serverHandler(req, res) {
  res.connection.destroy();
}

const server = http.createServer(serverHandler);
server.listen(0, common.mustCall(() => {
  for (let i = 0; i < cpus; i++)
    getAll();
}));

function getAll() {
  if (!createClients)
    return;

  const req = http.get({
    hostname: 'localhost',
    pathname: '/',
    port: server.address().port
  }, cb).on('error', cb);

  count++;
  onGC(req, { ongc });

  setImmediate(getAll);
}

function cb(res) {
  done += 1;
}

function ongc() {
  countGC++;
}

setImmediate(status);

function status() {
  if (done > 0) {
    createClients = false;
    globalThis.gc();
    console.log(`done/collected/total: ${done}/${countGC}/${count}`);
    if (countGC === count) {
      server.close();
      return;
    }
  }

  setImmediate(status);
}

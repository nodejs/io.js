'use strict';
// Flags: --expose-gc
// Like test-gc-http-client.js, but with a timeout set.

const common = require('../common');
const { onGC } = require('../common/gc');
const http = require('http');

function serverHandler(req, res) {
  setTimeout(function() {
    req.resume();
    res.writeHead(200);
    res.end('hello\n');
  }, 100);
}

const numRequests = 128;
let done = 0;
let countGC = 0;

const server = http.createServer(serverHandler);
server.listen(0, common.mustCall(() => {
  getAll(numRequests);
}));

function getAll(requestsRemaining) {
  if (requestsRemaining <= 0)
    return;

  const req = http.get({
    hostname: 'localhost',
    pathname: '/',
    port: server.address().port
  }, cb);

  req.setTimeout(10, common.mustCall());

  onGC(req, { ongc });

  setImmediate(getAll, requestsRemaining - 1);
}

function cb(res) {
  res.resume();
  done += 1;
}

function ongc() {
  countGC++;
}

setImmediate(status);

function status() {
  if (done > 0) {
    global.gc();
    console.log(`done/collected/total: ${done}/${countGC}/${numRequests}`);
    if (countGC === numRequests) {
      server.close();
      return;
    }
  }

  setImmediate(status);
}

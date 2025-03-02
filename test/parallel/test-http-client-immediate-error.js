'use strict';
// Flags: --expose-internals

// Make sure http.request() can catch immediate errors in
// net.createConnection().

const common = require('../common');
const assert = require('assert');
const net = require('net');
const http = require('http');
const { internalBinding } = require('internal/test/binding');
const { UV_ENETUNREACH, UV_EADDRINUSE } = internalBinding('uv');
const {
  newAsyncId,
  symbols: { async_id_symbol }
} = require('internal/async_hooks');

const config = {
  host: 'http://example.com',
  // We need to specify 'family' in both items of the array so 'internalConnectMultiple' is invoked
  connectMultiple: [{ address: '192.4.4.4', family: 4 }, { address: '200::1', family: 6 }],
  connectSolo: { address: '192.4.4.4', family: 4 },
};

function agentFactory(handle, count) {
  const agent = new http.Agent();
  agent.createConnection = common.mustCall((cfg) => {
    const sock = new net.Socket();

    // Fake the handle so we can enforce returning an immediate error
    sock._handle = handle;

    // Simulate just enough socket handle initialization
    sock[async_id_symbol] = newAsyncId();

    sock.connect(cfg);
    return sock;
  }, count);

  return agent;
};

const handleWithoutBind = {
  connect: common.mustCall((req, addr, port) => {
    return UV_ENETUNREACH;
  }, 3), // Because two tests will use this
  readStart() { },
  close() { },
};

const handleWithBind = {
  readStart() { },
  close() { },
  bind: common.mustCall(() => UV_EADDRINUSE, 2), // Because two tests will use this handle
};

const agentWithoutBind = agentFactory(handleWithoutBind, 3);
const agentWithBind = agentFactory(handleWithBind, 2);

http.get({
  host: '127.0.0.1',
  port: 1,
  agent: agentWithoutBind,
}).on('error', common.mustCall((err) => {
  assert.strictEqual(err.code, 'ENETUNREACH');
}));

http.request(config.host, {
  agent: agentWithoutBind,
  lookup(_0, _1, cb) {
    cb(null, config.connectMultiple);
  },
}).on('error', common.mustCall((err) => {
  assert.strictEqual(err.code, 'ENETUNREACH');
}));

http.request(config.host, {
  agent: agentWithoutBind,
  lookup(_0, _1, cb) {
    cb(null, config.connectSolo.address, config.connectSolo.family);
  },
  family: 4,
}).on('error', common.mustCall((err) => {
  assert.strictEqual(err.code, 'ENETUNREACH');
}));

http.request(config.host, {
  agent: agentWithBind,
  family: 4, // We specify 'family' so 'internalConnect' is invoked
  localPort: 2222 // Required to trigger _handle.bind()
}).on('error', common.mustCall((err) => {
  assert.strictEqual(err.code, 'EADDRINUSE');
}));

http.request(config.host, {
  agent: agentWithBind,
  lookup(_0, _1, cb) {
    cb(null, config.connectMultiple);
  },
  localPort: 2222, // Required to trigger _handle.bind()
}).on('error', common.mustCall((err) => {
  assert.strictEqual(err.code, 'EADDRINUSE');
}));

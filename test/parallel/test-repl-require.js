'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');
const net = require('net');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.chdir is not available in Workers');
}

process.chdir(fixtures.fixturesDir);
const repl = require('repl');

{
  const server = net.createServer((conn) => {
    repl.start('', conn).on('exit', () => {
      conn.destroy();
      server.close();
    });
  });

  const host = common.localhostIPv4;
  const port = 0;
  const options = { host, port };

  let answer = '';
  server.listen(options, function() {
    options.port = this.address().port;
    const conn = net.connect(options);
    conn.setEncoding('utf8');
    conn.on('data', (data) => answer += data);
    conn.write('require("baz")\nrequire("./baz")\n.exit\n');
  });

  process.on('exit', function() {
    assert.doesNotMatch(answer, /Cannot find module/);
    assert.doesNotMatch(answer, /Error/);
    assert.strictEqual(answer, '\'eye catcher\'\n\'perhaps I work\'\n');
  });
}

// Test for https://github.com/nodejs/node/issues/30808
// In REPL, we shouldn't look up relative modules from 'node_modules'.
{
  const server = net.createServer((conn) => {
    repl.start('', conn).on('exit', () => {
      conn.destroy();
      server.close();
    });
  });

  const host = common.localhostIPv4;
  const port = 0;
  const options = { host, port };

  let answer = '';
  server.listen(options, function() {
    options.port = this.address().port;
    const conn = net.connect(options);
    conn.setEncoding('utf8');
    conn.on('data', (data) => answer += data);
    conn.write('require("./bar")\n.exit\n');
  });

  process.on('exit', function() {
    assert.match(answer, /Uncaught Error: Cannot find module '\.\/bar'/);

    assert.match(answer, /code: 'MODULE_NOT_FOUND'/);
    assert.match(answer, /requireStack: \[ '<repl>' \]/);
  });
}

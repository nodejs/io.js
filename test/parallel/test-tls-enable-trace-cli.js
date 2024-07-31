// Flags: --expose-internals
'use strict';
const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');
const fixtures = require('../common/fixtures');

// Test --trace-tls CLI flag.

const assert = require('assert');
const { fork } = require('child_process');

if (process.argv[2] === 'test')
  return test();

const binding = require('internal/test/binding').internalBinding;

if (!binding('tls_wrap').HAVE_SSL_TRACE)
  return common.skip('no SSL_trace() compiled into openssl');

const child = fork(__filename, ['test'], {
  silent: true,
  execArgv: ['--trace-tls']
});

let stdout = '';
let stderr = '';
child.stdout.setEncoding('utf8');
child.stderr.setEncoding('utf8');
child.stdout.on('data', (data) => stdout += data);
child.stderr.on('data', (data) => stderr += data);
child.on('close', common.mustCall((code, signal) => {
  // For debugging and observation of actual trace output.
  console.log(stderr);

  assert.strictEqual(code, 0);
  assert.strictEqual(signal, null);
  assert.strictEqual(stdout.trim(), '');
  assert.match(stderr, /Warning: Enabling --trace-tls can expose sensitive/);
  assert.match(stderr, /Sent (?:TLS )?Record/);
}));

function test() {
  const {
    connect, keys
  } = require(fixtures.path('tls-connect'));

  connect({
    client: {
      checkServerIdentity: (servername, cert) => { },
      ca: `${keys.agent1.cert}\n${keys.agent6.ca}`,
    },
    server: {
      cert: keys.agent6.cert,
      key: keys.agent6.key
    },
  }, common.mustCall((err, pair, cleanup) => {
    if (pair.server.err) {
      console.trace('server', pair.server.err);
    }
    if (pair.client.err) {
      console.trace('client', pair.client.err);
    }
    assert.ifError(pair.server.err);
    assert.ifError(pair.client.err);

    return cleanup();
  }));
}

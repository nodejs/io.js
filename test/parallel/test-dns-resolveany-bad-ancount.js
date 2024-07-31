'use strict';
const common = require('../common');
const dnstools = require('../common/dns');
const dns = require('dns');
const assert = require('assert');
const dgram = require('dgram');
const dnsPromises = dns.promises;

const server = dgram.createSocket('udp4');
const resolver = new dns.Resolver({ timeout: 100, tries: 1 });
const resolverPromises = new dnsPromises.Resolver({ timeout: 100, tries: 1 });

server.on('message', common.mustCall((msg, { address, port }) => {
  const parsed = dnstools.parseDNSPacket(msg);
  const domain = parsed.questions[0].domain;
  assert.strictEqual(domain, 'example.org');

  const buf = dnstools.writeDNSPacket({
    id: parsed.id,
    questions: parsed.questions,
    answers: { type: 'A', address: '1.2.3.4', ttl: 123, domain },
  });
  // Overwrite the # of answers with 2, which is incorrect. The response is
  // discarded in c-ares >= 1.21.0. This is the reason why a small timeout is
  // used in the `Resolver` constructor. See
  // https://github.com/nodejs/node/pull/50743#issue-1994909204
  buf.writeUInt16LE(2, 6);
  server.send(buf, port, address);
}, 2));

server.bind(0, common.mustCall(async () => {
  const address = server.address();
  resolver.setServers([`127.0.0.1:${address.port}`]);
  resolverPromises.setServers([`127.0.0.1:${address.port}`]);

  resolverPromises.resolveAny('example.org')
    .then(common.mustNotCall())
    .catch(common.expectsError({
      // May return EBADRESP or ETIMEOUT
      code: /^(?:EBADRESP|ETIMEOUT)$/,
      syscall: 'queryAny',
      hostname: 'example.org'
    }));

  resolver.resolveAny('example.org', common.mustCall((err) => {
    assert.notStrictEqual(err.code, 'SUCCESS');
    assert.strictEqual(err.syscall, 'queryAny');
    assert.strictEqual(err.hostname, 'example.org');
    const descriptor = Object.getOwnPropertyDescriptor(err, 'message');
    // The error message should be non-enumerable.
    assert.strictEqual(descriptor.enumerable, false);
    server.close();
  }));
}));

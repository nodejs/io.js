'use strict';
const common = require('../common');
const dnstools = require('../common/dns');
const dns = require('dns');
const assert = require('assert');
const dgram = require('dgram');
const dnsPromises = dns.promises;

const answers = [
  { type: 'A', address: '1.2.3.4', ttl: 123 },
  { type: 'AAAA', address: '::42', ttl: 123 },
  { type: 'MX', priority: 42, exchange: 'foobar.com', ttl: 124 },
  { type: 'NS', value: 'foobar.org', ttl: 457 },
  { type: 'TXT', entries: [ 'v=spf1 ~all xyz\0foo' ] },
  { type: 'PTR', value: 'baz.org', ttl: 987 },
  {
    type: 'SOA',
    nsname: 'ns1.example.com',
    hostmaster: 'admin.example.com',
    serial: 156696742,
    refresh: 900,
    retry: 900,
    expire: 1800,
    minttl: 60
  },
  {
    type: 'CAA',
    critical: 128,
    issue: 'platynum.ch'
  },
];

const server = dgram.createSocket('udp4');

server.on('message', common.mustCall((msg, { address, port }) => {
  const parsed = dnstools.parseDNSPacket(msg);
  const domain = parsed.questions[0].domain;
  assert.strictEqual(domain, 'example.org');

  server.send(dnstools.writeDNSPacket({
    id: parsed.id,
    questions: parsed.questions,
    answers: answers.map((answer) => Object.assign({ domain }, answer)),
  }), port, address);
}, 2));

server.bind(0, common.mustCall(async () => {
  const address = server.address();
  dns.setServers([`127.0.0.1:${address.port}`]);

  validateResults(await dnsPromises.resolveAny('example.org'));

  dns.resolveAny('example.org', common.mustSucceed((res) => {
    validateResults(res);
    server.close();
  }));
}));

function validateResults(res) {
  // TTL values are only provided for A and AAAA entries.
  assert.deepStrictEqual(res.map(maybeRedactTTL), answers.map(maybeRedactTTL));
}

function maybeRedactTTL(r) {
  const ret = { ...r };
  if (!['A', 'AAAA'].includes(r.type))
    delete ret.ttl;
  return ret;
}

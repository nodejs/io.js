// Flags: --experimental-quic --no-warnings
'use strict';

const { hasQuic } = require('../common');
const {
  deepStrictEqual
} = require('node:assert');
const {
  describe,
  it,
  beforeEach,
  afterEach
} = require('node:test');

const skip = !hasQuic;

const STRING_DATAGRAMS = ['hello', 'world']

const encoder = new TextEncoder()
const BUF_DATAGRAMS = STRING_DATAGRAMS.map(str => encoder.encode(str))

async function delay (ms) {
  await new Promise((resolve) => {
    setTimeout(() => {
      resolve()
    }, ms)
  })
}

async function sendDatagrams(sender, receiver, datagrams) {
  const allDatagramsReceived = Promise.withResolvers();
  const datagramsReceived = [];

  // TODO: currently only the first datagram is received, and it's received many
  // times so this test will not pass
  receiver.ondatagram = (datagram) => {
    datagramsReceived.push(datagram);

    if (datagramsReceived.length === datagrams.length) {
      allDatagramsReceived.resolve();
    }
  };

  for (const datagram of datagrams) {
    await sender.sendDatagram(datagram);
  }

  await allDatagramsReceived.promise;

  for (let i = 0; i < datagrams.length; i++) {
    deepStrictEqual(datagramsReceived[i], datagrams[i]);
  }
}

describe('quic datagrams', { skip }, async () => {
  const { createPrivateKey } = require('node:crypto');
  const fixtures = require('../common/fixtures');
  const keys = createPrivateKey(fixtures.readKey('agent1-key.pem'));
  const certs = fixtures.readKey('agent1-cert.pem');
  let serverEndpoint
  let clientSession
  let serverSession

  const {
    listen,
    connect,
  } = require('node:quic');

  beforeEach(async () => {
    const clientConnected = Promise.withResolvers();

    serverEndpoint = await listen((serverSession) => {
      serverSession.opened.then(() => {
        clientConnected.resolve(serverSession);
      });
    }, { keys, certs });

    clientSession = await connect(serverEndpoint.address);
    await clientSession.opened;

    serverSession = await clientConnected.promise;
  })

  afterEach(async () => {
    // TODO: calling `.close` on `serverEndpoint` without first closing both
    // sessions causes a segfault - we should be able to stop the listener
    // without manually closing all sessions first
    await clientSession?.close();
    await serverSession?.close();
    await serverEndpoint?.close();
  })

  it('a quic client can send ArrayBufferView datagrams to a server', async () => {
    // TODO: if this delay is removed, `can_send_packets` in `session.cc`
    // returns false and the datagram is not sent - after the `.opened` promise
    // resolves we should be able to send datagrams without additional delays
    await delay(500);

    await sendDatagrams(clientSession, serverSession, BUF_DATAGRAMS);
  });

  it('a quic server can send ArrayBufferView datagrams to a client', async () => {
    // TODO: if this delay is removed, `can_send_packets` in `session.cc`
    // returns false and the datagram is not sent - after the `.opened` promise
    // resolves we should be able to send datagrams without additional delays
    await delay(500);

    await sendDatagrams(serverSession, clientSession, BUF_DATAGRAMS);
  });

  it('a quic client can send String datagrams to a server', async () => {
    // TODO: if this delay is removed, `can_send_packets` in `session.cc`
    // returns false and the datagram is not sent - after the `.opened` promise
    // resolves we should be able to send datagrams without additional delays
    await delay(500);

    await sendDatagrams(clientSession, serverSession, STRING_DATAGRAMS);
  });

  it('a quic server can send String datagrams to a client', async () => {
    // TODO: if this delay is removed, `can_send_packets` in `session.cc`
    // returns false and the datagram is not sent - after the `.opened` promise
    // resolves we should be able to send datagrams without additional delays
    await delay(500);

    await sendDatagrams(serverSession, clientSession, STRING_DATAGRAMS);
  });
});

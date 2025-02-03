// Flags: --expose-internals
'use strict';

const common = require('../common');
const {
  ok,
  strictEqual,
  throws,
} = require('assert');
const {
  SocketAddress,
} = require('net');

const {
  InternalSocketAddress,
} = require('internal/socketaddress');
const { internalBinding } = require('internal/test/binding');
const {
  SocketAddress: _SocketAddress,
  AF_INET,
} = internalBinding('block_list');

const { describe, it } = require('node:test');

describe('net.SocketAddress...', () => {

  it('is cloneable', () => {
    const sa = new SocketAddress();
    strictEqual(sa.address, '127.0.0.1');
    strictEqual(sa.port, 0);
    strictEqual(sa.family, 'ipv4');
    strictEqual(sa.flowlabel, 0);

    const mc = new MessageChannel();
    mc.port1.onmessage = common.mustCall(({ data }) => {
      ok(SocketAddress.isSocketAddress(data));

      strictEqual(data.address, '127.0.0.1');
      strictEqual(data.port, 0);
      strictEqual(data.family, 'ipv4');
      strictEqual(data.flowlabel, 0);

      mc.port1.close();
    });
    mc.port2.postMessage(sa);
  });

  it('has reasonable defaults', () => {
    const sa = new SocketAddress({});
    strictEqual(sa.address, '127.0.0.1');
    strictEqual(sa.port, 0);
    strictEqual(sa.family, 'ipv4');
    strictEqual(sa.flowlabel, 0);
  });

  it('interprets simple ipv4 correctly', () => {
    const sa = new SocketAddress({
      address: '123.123.123.123',
    });
    strictEqual(sa.address, '123.123.123.123');
    strictEqual(sa.port, 0);
    strictEqual(sa.family, 'ipv4');
    strictEqual(sa.flowlabel, 0);
  });

  it('sets the port correctly', () => {
    const sa = new SocketAddress({
      address: '123.123.123.123',
      port: 80
    });
    strictEqual(sa.address, '123.123.123.123');
    strictEqual(sa.port, 80);
    strictEqual(sa.family, 'ipv4');
    strictEqual(sa.flowlabel, 0);
  });

  it('interprets simple ipv6 correctly', () => {
    const sa = new SocketAddress({
      family: 'ipv6'
    });
    strictEqual(sa.address, '::');
    strictEqual(sa.port, 0);
    strictEqual(sa.family, 'ipv6');
    strictEqual(sa.flowlabel, 0);
  });

  it('uses the flowlabel correctly', () => {
    const sa = new SocketAddress({
      family: 'ipv6',
      flowlabel: 1,
    });
    strictEqual(sa.address, '::');
    strictEqual(sa.port, 0);
    strictEqual(sa.family, 'ipv6');
    strictEqual(sa.flowlabel, 1);
  });

  it('validates input correctly', () => {
    [1, false, 'hello'].forEach((i) => {
      throws(() => new SocketAddress(i), {
        code: 'ERR_INVALID_ARG_TYPE'
      });
    });

    [1, false, {}, [], 'test'].forEach((family) => {
      throws(() => new SocketAddress({ family }), {
        code: 'ERR_INVALID_ARG_VALUE'
      });
    });

    [1, false, {}, []].forEach((address) => {
      throws(() => new SocketAddress({ address }), {
        code: 'ERR_INVALID_ARG_TYPE'
      });
    });

    [-1, false, {}, []].forEach((port) => {
      throws(() => new SocketAddress({ port }), {
        code: 'ERR_SOCKET_BAD_PORT'
      });
    });

    throws(() => new SocketAddress({ flowlabel: -1 }), {
      code: 'ERR_OUT_OF_RANGE'
    });
  });

  it('InternalSocketAddress correctly inherits from SocketAddress', () => {
    // Test that the internal helper class InternalSocketAddress correctly
    // inherits from SocketAddress and that it does not throw when its properties
    // are accessed.

    const address = '127.0.0.1';
    const port = 8080;
    const flowlabel = 0;
    const handle = new _SocketAddress(address, port, AF_INET, flowlabel);
    const addr = new InternalSocketAddress(handle);
    ok(addr instanceof SocketAddress);
    strictEqual(addr.address, address);
    strictEqual(addr.port, port);
    strictEqual(addr.family, 'ipv4');
    strictEqual(addr.flowlabel, flowlabel);
  });

  it('SocketAddress.parse() works as expected', () => {
    const good = [
      { input: '1.2.3.4', address: '1.2.3.4', port: 0, family: 'ipv4' },
      { input: '192.168.257:1', address: '192.168.1.1', port: 1, family: 'ipv4' },
      { input: '256', address: '0.0.1.0', port: 0, family: 'ipv4' },
      { input: '999999999:12', address: '59.154.201.255', port: 12, family: 'ipv4' },
      { input: '0xffffffff', address: '255.255.255.255', port: 0, family: 'ipv4' },
      { input: '0x.0x.0', address: '0.0.0.0', port: 0, family: 'ipv4' },
      { input: '[1:0::]', address: '1::', port: 0, family: 'ipv6' },
      { input: '[1::8]:123', address: '1::8', port: 123, family: 'ipv6' },
    ];

    good.forEach((i) => {
      const addr = SocketAddress.parse(i.input);
      strictEqual(addr.address, i.address);
      strictEqual(addr.port, i.port);
      strictEqual(addr.family, i.family);
    });

    const bad = [
      'not an ip',
      'abc.123',
      '259.1.1.1',
      '12:12:12',
    ];

    bad.forEach((i) => {
      strictEqual(SocketAddress.parse(i), undefined);
    });
  });

});

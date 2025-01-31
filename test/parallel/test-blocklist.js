'use strict';

require('../common');

const {
  BlockList,
  SocketAddress,
} = require('net');
const assert = require('assert');
const util = require('util');

{
  const blockList = new BlockList();

  [1, [], {}, null, 1n, undefined, null].forEach((i) => {
    assert.throws(() => blockList.addAddress(i), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  [1, [], {}, null, 1n, null].forEach((i) => {
    assert.throws(() => blockList.addAddress('1.1.1.1', i), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  assert.throws(() => blockList.addAddress('1.1.1.1', 'foo'), {
    code: 'ERR_INVALID_ARG_VALUE'
  });

  [1, [], {}, null, 1n, undefined, null].forEach((i) => {
    assert.throws(() => blockList.addRange(i), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
    assert.throws(() => blockList.addRange('1.1.1.1', i), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  [1, [], {}, null, 1n, null].forEach((i) => {
    assert.throws(() => blockList.addRange('1.1.1.1', '1.1.1.2', i), {
      code: 'ERR_INVALID_ARG_TYPE'
    });
  });

  assert.throws(() => blockList.addRange('1.1.1.1', '1.1.1.2', 'foo'), {
    code: 'ERR_INVALID_ARG_VALUE'
  });
}

{
  const blockList = new BlockList();
  blockList.addAddress('1.1.1.1');
  blockList.addAddress('8592:757c:efae:4e45:fb5d:d62a:0d00:8e17', 'ipv6');
  blockList.addAddress('::ffff:1.1.1.2', 'ipv6');

  assert(blockList.check('1.1.1.1'));
  assert(!blockList.check('1.1.1.1', 'ipv6'));
  assert(!blockList.check('8592:757c:efae:4e45:fb5d:d62a:0d00:8e17'));
  assert(blockList.check('8592:757c:efae:4e45:fb5d:d62a:0d00:8e17', 'ipv6'));

  assert(blockList.check('::ffff:1.1.1.1', 'ipv6'));
  assert(blockList.check('::ffff:1.1.1.1', 'IPV6'));

  assert(blockList.check('1.1.1.2'));

  assert(!blockList.check('1.2.3.4'));
  assert(!blockList.check('::1', 'ipv6'));
}

{
  const blockList = new BlockList();
  const sa1 = new SocketAddress({ address: '1.1.1.1' });
  const sa2 = new SocketAddress({
    address: '8592:757c:efae:4e45:fb5d:d62a:0d00:8e17',
    family: 'ipv6'
  });
  const sa3 = new SocketAddress({ address: '1.1.1.2' });

  blockList.addAddress(sa1);
  blockList.addAddress(sa2);
  blockList.addAddress('::ffff:1.1.1.2', 'ipv6');

  assert(blockList.check('1.1.1.1'));
  assert(blockList.check(sa1));
  assert(!blockList.check('1.1.1.1', 'ipv6'));
  assert(!blockList.check('8592:757c:efae:4e45:fb5d:d62a:0d00:8e17'));
  assert(blockList.check('8592:757c:efae:4e45:fb5d:d62a:0d00:8e17', 'ipv6'));
  assert(blockList.check(sa2));

  assert(blockList.check('::ffff:1.1.1.1', 'ipv6'));
  assert(blockList.check('::ffff:1.1.1.1', 'IPV6'));

  assert(blockList.check('1.1.1.2'));
  assert(blockList.check(sa3));

  assert(!blockList.check('1.2.3.4'));
  assert(!blockList.check('::1', 'ipv6'));
}

{
  const blockList = new BlockList();
  blockList.addRange('1.1.1.1', '1.1.1.10');
  blockList.addRange('::1', '::f', 'ipv6');

  assert(!blockList.check('1.1.1.0'));
  for (let n = 1; n <= 10; n++)
    assert(blockList.check(`1.1.1.${n}`));
  assert(!blockList.check('1.1.1.11'));

  assert(!blockList.check('::0', 'ipv6'));
  for (let n = 0x1; n <= 0xf; n++) {
    assert(blockList.check(`::${n.toString(16)}`, 'ipv6'),
           `::${n.toString(16)} check failed`);
  }
  assert(!blockList.check('::10', 'ipv6'));
}

{
  const blockList = new BlockList();
  const sa1 = new SocketAddress({ address: '1.1.1.1' });
  const sa2 = new SocketAddress({ address: '1.1.1.10' });
  const sa3 = new SocketAddress({ address: '::1', family: 'ipv6' });
  const sa4 = new SocketAddress({ address: '::f', family: 'ipv6' });

  blockList.addRange(sa1, sa2);
  blockList.addRange(sa3, sa4);

  assert(!blockList.check('1.1.1.0'));
  for (let n = 1; n <= 10; n++)
    assert(blockList.check(`1.1.1.${n}`));
  assert(!blockList.check('1.1.1.11'));

  assert(!blockList.check('::0', 'ipv6'));
  for (let n = 0x1; n <= 0xf; n++) {
    assert(blockList.check(`::${n.toString(16)}`, 'ipv6'),
           `::${n.toString(16)} check failed`);
  }
  assert(!blockList.check('::10', 'ipv6'));
}

{
  const blockList = new BlockList();
  blockList.addSubnet('1.1.1.0', 16);
  blockList.addSubnet('8592:757c:efae:4e45::', 64, 'ipv6');

  assert(blockList.check('1.1.0.1'));
  assert(blockList.check('1.1.1.1'));
  assert(!blockList.check('1.2.0.1'));
  assert(blockList.check('::ffff:1.1.0.1', 'ipv6'));

  assert(blockList.check('8592:757c:efae:4e45:f::', 'ipv6'));
  assert(blockList.check('8592:757c:efae:4e45::f', 'ipv6'));
  assert(!blockList.check('8592:757c:efae:4f45::f', 'ipv6'));
}

{
  const blockList = new BlockList();
  const sa1 = new SocketAddress({ address: '1.1.1.0' });
  const sa2 = new SocketAddress({ address: '1.1.1.1' });
  blockList.addSubnet(sa1, 16);
  blockList.addSubnet('8592:757c:efae:4e45::', 64, 'ipv6');

  assert(blockList.check('1.1.0.1'));
  assert(blockList.check(sa2));
  assert(!blockList.check('1.2.0.1'));
  assert(blockList.check('::ffff:1.1.0.1', 'ipv6'));

  assert(blockList.check('8592:757c:efae:4e45:f::', 'ipv6'));
  assert(blockList.check('8592:757c:efae:4e45::f', 'ipv6'));
  assert(!blockList.check('8592:757c:efae:4f45::f', 'ipv6'));
}

{
  const blockList = new BlockList();
  blockList.addAddress('1.1.1.1');
  blockList.addRange('10.0.0.1', '10.0.0.10');
  blockList.addSubnet('8592:757c:efae:4e45::', 64, 'IpV6'); // Case insensitive

  const rulesCheck = [
    'Subnet: IPv6 8592:757c:efae:4e45::/64',
    'Range: IPv4 10.0.0.1-10.0.0.10',
    'Address: IPv4 1.1.1.1',
  ];
  assert.deepStrictEqual(blockList.rules, rulesCheck);

  assert(blockList.check('1.1.1.1'));
  assert(blockList.check('10.0.0.5'));
  assert(blockList.check('::ffff:10.0.0.5', 'ipv6'));
  assert(blockList.check('8592:757c:efae:4e45::f', 'ipv6'));

  assert(!blockList.check('123.123.123.123'));
  assert(!blockList.check('8592:757c:efaf:4e45:fb5d:d62a:0d00:8e17', 'ipv6'));
  assert(!blockList.check('::ffff:123.123.123.123', 'ipv6'));
}

{
  // This test validates boundaries of non-aligned CIDR bit prefixes
  const blockList = new BlockList();
  blockList.addSubnet('10.0.0.0', 27);
  blockList.addSubnet('8592:757c:efaf::', 51, 'ipv6');

  for (let n = 0; n <= 31; n++)
    assert(blockList.check(`10.0.0.${n}`));
  assert(!blockList.check('10.0.0.32'));

  assert(blockList.check('8592:757c:efaf:0:0:0:0:0', 'ipv6'));
  assert(blockList.check('8592:757c:efaf:1fff:ffff:ffff:ffff:ffff', 'ipv6'));
  assert(!blockList.check('8592:757c:efaf:2fff:ffff:ffff:ffff:ffff', 'ipv6'));
}

{
  // Regression test for https://github.com/nodejs/node/issues/39074
  const blockList = new BlockList();

  blockList.addRange('10.0.0.2', '10.0.0.10');

  // IPv4 checks against IPv4 range.
  assert(blockList.check('10.0.0.2'));
  assert(blockList.check('10.0.0.10'));
  assert(!blockList.check('192.168.0.3'));
  assert(!blockList.check('2.2.2.2'));
  assert(!blockList.check('255.255.255.255'));

  // IPv6 checks against IPv4 range.
  assert(blockList.check('::ffff:0a00:0002', 'ipv6'));
  assert(blockList.check('::ffff:0a00:000a', 'ipv6'));
  assert(!blockList.check('::ffff:c0a8:0003', 'ipv6'));
  assert(!blockList.check('::ffff:0202:0202', 'ipv6'));
  assert(!blockList.check('::ffff:ffff:ffff', 'ipv6'));
}

{
  const blockList = new BlockList();
  assert.throws(() => blockList.addRange('1.1.1.2', '1.1.1.1'), /ERR_INVALID_ARG_VALUE/);
}

{
  const blockList = new BlockList();
  assert.throws(() => blockList.addSubnet(1), /ERR_INVALID_ARG_TYPE/);
  assert.throws(() => blockList.addSubnet('1.1.1.1', ''),
                /ERR_INVALID_ARG_TYPE/);
  assert.throws(() => blockList.addSubnet('1.1.1.1', NaN), /ERR_OUT_OF_RANGE/);
  assert.throws(() => blockList.addSubnet('', 1, 1), /ERR_INVALID_ARG_TYPE/);
  assert.throws(() => blockList.addSubnet('', 1, ''), /ERR_INVALID_ARG_VALUE/);

  assert.throws(() => blockList.addSubnet('1.1.1.1', -1, 'ipv4'),
                /ERR_OUT_OF_RANGE/);
  assert.throws(() => blockList.addSubnet('1.1.1.1', 33, 'ipv4'),
                /ERR_OUT_OF_RANGE/);

  assert.throws(() => blockList.addSubnet('::', -1, 'ipv6'),
                /ERR_OUT_OF_RANGE/);
  assert.throws(() => blockList.addSubnet('::', 129, 'ipv6'),
                /ERR_OUT_OF_RANGE/);
}

{
  const blockList = new BlockList();
  assert.throws(() => blockList.check(1), /ERR_INVALID_ARG_TYPE/);
  assert.throws(() => blockList.check('', 1), /ERR_INVALID_ARG_TYPE/);
}

{
  const blockList = new BlockList();
  const ret = util.inspect(blockList, { depth: -1 });
  assert.strictEqual(ret, '[BlockList]');
}

{
  const blockList = new BlockList();
  const ret = util.inspect(blockList, { depth: null });
  assert(ret.includes('rules: []'));
}

{
  // Test for https://github.com/nodejs/node/issues/43360
  const blocklist = new BlockList();
  blocklist.addSubnet('1.1.1.1', 32, 'ipv4');

  assert(blocklist.check('1.1.1.1'));
  assert(!blocklist.check('1.1.1.2'));
  assert(!blocklist.check('2.3.4.5'));
}

{
  assert(BlockList.isBlockList(new BlockList()));
  assert(!BlockList.isBlockList({}));
}

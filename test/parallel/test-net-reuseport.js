'use strict';
const common = require('../common');
const { checkSupportReusePort, options } = require('../common/net');
const net = require('net');

function test(host) {
  const server1 = net.createServer();
  const server2 = net.createServer();
  server1.listen({ ...options, host }, common.mustCall(() => {
    const port = server1.address().port;
    server2.listen({ ...options, host, port }, common.mustCall(() => {
      server1.close();
      server2.close();
    }));
  }));
}

checkSupportReusePort()
.then(() => {
  test('127.0.0.1');
  common.hasIPv6 && test('::');
}, () => {
  common.skip('The `reusePort` option is not supported');
});

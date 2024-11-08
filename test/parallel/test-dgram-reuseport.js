'use strict';
const common = require('../common');
const { checkSupportReusePort, options } = require('../common/udp');
const dgram = require('dgram');

function test() {
  const socket1 = dgram.createSocket(options);
  const socket2 = dgram.createSocket(options);
  socket1.bind(0, common.mustCall(() => {
    socket2.bind(socket1.address().port, common.mustCall(() => {
      socket1.close();
      socket2.close();
    }));
  }));
}

checkSupportReusePort().then(test, () => {
  common.skip('The `reusePort` option is not supported');
});

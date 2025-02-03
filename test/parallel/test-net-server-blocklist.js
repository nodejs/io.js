'use strict';
const common = require('../common');
const net = require('net');

const blockList = new net.BlockList();
blockList.addAddress(common.localhostIPv4);

const server = net.createServer({ blockList }, common.mustNotCall());
server.listen(0, common.localhostIPv4, common.mustCall(() => {
  const adddress = server.address();
  const socket = net.connect({
    localAddress: common.localhostIPv4,
    host: adddress.address,
    port: adddress.port
  });
  socket.on('close', common.mustCall(() => {
    server.close();
  }));
}));

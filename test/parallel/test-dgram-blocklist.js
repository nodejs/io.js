'use strict';
const common = require('../common');
const assert = require('assert');
const dgram = require('dgram');
const net = require('net');

{
  const blockList = new net.BlockList();
  blockList.addAddress(common.localhostIPv4);

  const connectSocket = dgram.createSocket({ type: 'udp4', sendBlockList: blockList });
  connectSocket.connect(9999, common.localhostIPv4, common.mustCall((err) => {
    assert.ok(err.code === 'ERR_IP_BLOCKED', err);
    connectSocket.close();
  }));
}

{
  const blockList = new net.BlockList();
  blockList.addAddress(common.localhostIPv4);
  const sendSocket = dgram.createSocket({ type: 'udp4', sendBlockList: blockList });
  sendSocket.send('hello', 9999, common.localhostIPv4, common.mustCall((err) => {
    assert.ok(err.code === 'ERR_IP_BLOCKED', err);
    sendSocket.close();
  }));
}

{
  const blockList = new net.BlockList();
  blockList.addAddress(common.localhostIPv4);
  const receiveSocket = dgram.createSocket({ type: 'udp4', receiveBlockList: blockList });
  // Hack to close the socket
  const check = blockList.check;
  blockList.check = function() {
    process.nextTick(() => {
      receiveSocket.close();
    });
    return check.apply(this, arguments);
  };
  receiveSocket.on('message', common.mustNotCall());
  receiveSocket.bind(0, common.localhostIPv4, common.mustCall(() => {
    const addressInfo = receiveSocket.address();
    const client = dgram.createSocket('udp4');
    client.send('hello', addressInfo.port, addressInfo.address, common.mustCall((err) => {
      assert.ok(!err);
      client.close();
    }));
  }));
}

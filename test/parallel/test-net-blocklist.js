'use strict';

const common = require('../common');
const net = require('net');
const assert = require('assert');

const blockList = new net.BlockList();
blockList.addAddress('127.0.0.1');
blockList.addAddress('127.0.0.2');

function check(err) {
  assert.ok(err.code === 'ERR_IP_BLOCKED', err);
}

// Connect without calling dns.lookup
{
  const socket = net.connect({
    port: 9999,
    host: '127.0.0.1',
    blockList,
  });
  socket.on('error', common.mustCall(check));
}

// Connect with single IP returned by dns.lookup
{
  const socket = net.connect({
    port: 9999,
    host: 'localhost',
    blockList,
    lookup: function(_, __, cb) {
      cb(null, '127.0.0.1', 4);
    },
    autoSelectFamily: false,
  });

  socket.on('error', common.mustCall(check));
}

// Connect with autoSelectFamily and single IP
{
  const socket = net.connect({
    port: 9999,
    host: 'localhost',
    blockList,
    lookup: function(_, __, cb) {
      cb(null, [{ address: '127.0.0.1', family: 4 }]);
    },
    autoSelectFamily: true,
  });

  socket.on('error', common.mustCall(check));
}

// Connect with autoSelectFamily and multiple IPs
{
  const socket = net.connect({
    port: 9999,
    host: 'localhost',
    blockList,
    lookup: function(_, __, cb) {
      cb(null, [{ address: '127.0.0.1', family: 4 }, { address: '127.0.0.2', family: 4 }]);
    },
    autoSelectFamily: true,
  });

  socket.on('error', common.mustCall(check));
}

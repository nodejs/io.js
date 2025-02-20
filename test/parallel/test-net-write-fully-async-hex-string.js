'use strict';
// Flags: --expose-gc

// Regression test for https://github.com/nodejs/node/issues/8251.
const common = require('../common');
const net = require('net');

const data = Buffer.alloc(1000000).toString('hex');

const server = net.createServer(common.mustCall(function(conn) {
  conn.resume();
  server.close();
})).listen(0, common.mustCall(function() {
  const conn = net.createConnection(this.address().port, common.mustCall(() => {
    let count = 0;

    function writeLoop() {
      if (count++ === 20) {
        conn.end();
        return;
      }

      while (conn.write(data, 'hex'));
      globalThis.gc({ type: 'major' });
      // The buffer allocated inside the .write() call should still be alive.
    }

    conn.on('drain', writeLoop);

    writeLoop();
  }));
}));

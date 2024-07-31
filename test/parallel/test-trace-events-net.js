'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

const CODE = `
  const net = require('net');
  const socket = net.connect('${common.PIPE}');
  socket.on('error', () => {});
  const server = net.createServer((socket) => {
    socket.destroy();
    server.close();
  }).listen(0, () => {
    net.connect(server.address().port);
  });
`;

tmpdir.refresh();
const FILE_NAME = tmpdir.resolve('node_trace.1.log');

const proc = cp.spawn(process.execPath,
                      [ '--trace-events-enabled',
                        '--trace-event-categories', 'node.net.native',
                        '-e', CODE ],
                      { cwd: tmpdir.path });

proc.once('exit', common.mustCall(() => {
  assert(fs.existsSync(FILE_NAME));
  fs.readFile(FILE_NAME, common.mustCall((err, data) => {
    const traces = JSON.parse(data.toString()).traceEvents;
    assert(traces.length > 0);
    let count = 0;
    for (const trace of traces) {
      if (trace.cat === 'node,node.net,node.net.native' && trace.name === 'connect') {
        count++;
      }
    }
    // Two begin, two end
    assert.strictEqual(count, 4);
  }));
}));

'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

const CODE = `
  const http = require('http');
  const server = http.createServer((req, res) => {
    res.end('ok');
    server.close();
  }).listen(0, () => {
    http.get({port: server.address().port});
  });
`;

tmpdir.refresh();
const FILE_NAME = tmpdir.resolve('node_trace.1.log');

const proc = cp.spawn(process.execPath,
                      [ '--trace-events-enabled',
                        '--trace-event-categories', 'node.http',
                        '-e', CODE ],
                      { cwd: tmpdir.path });

proc.once('exit', common.mustCall(() => {
  assert(fs.existsSync(FILE_NAME));
  fs.readFile(FILE_NAME, common.mustCall((err, data) => {
    assert(!err);
    const traces = JSON.parse(data.toString()).traceEvents;
    assert(traces.length > 0);
    let count = 0;
    for (const trace of traces) {
      if (trace.cat === 'node,node.http' &&
          ['http.server.request', 'http.client.request'].includes(trace.name)) {
        count++;
      }
    }
    // Two begin, two end
    assert.strictEqual(count, 4);
  }));
}));

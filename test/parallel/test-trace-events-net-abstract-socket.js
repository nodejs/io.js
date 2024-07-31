'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

if (!common.isLinux) common.skip();

const CODE = `
  const net = require('net');
  net.connect('${common.PIPE}').on('error', () => {});
  net.connect('\\0${common.PIPE}').on('error', () => {});
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
    traces.forEach((trace) => {
      if (trace.cat === 'node,node.net,node.net.native' &&
          trace.name === 'connect') {
        count++;
        if (trace.ph === 'b') {
          assert.ok(!!trace.args.path_type);
          assert.ok(!!trace.args.pipe_path);
        }
      }
    });
    assert.strictEqual(count, 4);
  }));
}));

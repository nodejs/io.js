'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

// Tests that node.console trace events for counters and time methods are
// emitted as expected.

const names = [
  'time::foo',
  'time::\\winvalid_char',
  'count::bar',
];
const expectedCounts = [ 1, 2, 0 ];
const expectedTimeTypes = [ 'b', 'n', 'e' ];

if (process.argv[2] === 'child') {
  // The following console outputs exercise the test, causing node.console
  // trace events to be emitted for the counter and time calls.
  console.count('bar');
  console.count('bar');
  console.countReset('bar');
  console.time('\\winvalid_char');
  console.time('foo');
  setImmediate(() => {
    console.timeEnd('\\winvalid_char');
    console.timeLog('foo');
    setImmediate(() => {
      console.timeEnd('foo');
    });
  });
} else {
  tmpdir.refresh();

  const proc = cp.fork(__filename,
                       [ 'child' ], {
                         cwd: tmpdir.path,
                         execArgv: [
                           '--trace-event-categories',
                           'node.console',
                         ]
                       });

  proc.once('exit', common.mustCall(async () => {
    const file = tmpdir.resolve('node_trace.1.log');

    assert(fs.existsSync(file));
    const data = await fs.promises.readFile(file, { encoding: 'utf8' });
    JSON.parse(data).traceEvents
      .filter((trace) => trace.cat !== '__metadata')
      .forEach((trace) => {
        assert.strictEqual(trace.pid, proc.pid);
        assert(names.includes(trace.name));
        if (trace.name === 'count::bar')
          assert.strictEqual(trace.args.data, expectedCounts.shift());
        else if (trace.name === 'time::foo')
          assert.strictEqual(trace.ph, expectedTimeTypes.shift());
      });
    assert.strictEqual(expectedCounts.length, 0);
    assert.strictEqual(expectedTimeTypes.length, 0);
  }));
}

'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const { isMainThread } = require('worker_threads');

if (isMainThread) {
  const CODE = 'const { Worker } = require(\'worker_threads\'); ' +
               `new Worker(${JSON.stringify(__filename)})`;
  const FILE_NAME = 'node_trace.1.log';
  const tmpdir = require('../common/tmpdir');
  tmpdir.refresh();
  process.chdir(tmpdir.path);

  const proc = cp.spawn(process.execPath,
                        [ '--trace-event-categories', 'node',
                          '-e', CODE ]);
  proc.once('exit', common.mustCall(() => {
    assert(fs.existsSync(FILE_NAME));
    fs.readFile(FILE_NAME, common.mustCall((err, data) => {
      const traces = JSON.parse(data.toString()).traceEvents;
      assert(traces.length > 0);
      assert(traces.some((trace) =>
        trace.cat === '__metadata' && trace.name === 'thread_name' &&
          trace.args.name === '[worker 1] WorkerThread'));
    }));
  }));
} else {
  // Do nothing here.
}

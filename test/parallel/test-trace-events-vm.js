'use strict';
const common = require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

const names = [
  'ContextifyScript::New',
  'RunInContext',
];

if (process.argv[2] === 'child') {
  const vm = require('vm');
  vm.runInNewContext('1 + 1');
} else {
  tmpdir.refresh();

  const proc = cp.fork(__filename,
                       [ 'child' ], {
                         cwd: tmpdir.path,
                         execArgv: [
                           '--trace-event-categories',
                           'node.vm.script',
                         ]
                       });

  proc.once('exit', common.mustCall(() => {
    const file = tmpdir.resolve('node_trace.1.log');

    assert(fs.existsSync(file));
    fs.readFile(file, common.mustCall((err, data) => {
      const traces = JSON.parse(data.toString()).traceEvents
        .filter((trace) => trace.cat !== '__metadata');
      for (const trace of traces) {
        assert.strictEqual(trace.pid, proc.pid);
        assert(names.includes(trace.name));
      }
    }));
  }));
}

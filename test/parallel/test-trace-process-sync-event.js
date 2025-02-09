'use strict';
require('../common');
const assert = require('assert');
const cp = require('child_process');
const fs = require('fs');
const path = require('path');
const tmpdir = require('../common/tmpdir');

if (process.env.isChild === '1') {
  cp.execFileSync('node', ['-e', '']);
  cp.execSync('node', ['-e', '']);
  cp.spawnSync('node', ['-e', '']);
  return;
}

tmpdir.refresh();
const FILE_NAME = path.join(tmpdir.path, 'node_trace.1.log');

cp.spawnSync(process.execPath,
             [
               '--trace-events-enabled',
               '--trace-event-categories', 'node.process.sync',
               __filename,
             ],
             {
               cwd: tmpdir.path,
               env: {
                 ...process.env,
                 isChild: '1',
               },
             });

assert(fs.existsSync(FILE_NAME));
const data = fs.readFileSync(FILE_NAME);
const traces = JSON.parse(data.toString()).traceEvents;
assert(traces.length > 0);
const count = traces.filter((item) => item.name === 'spawnSync').length;
// Two begin, Two end
assert.strictEqual(count === 3 * 2, true);

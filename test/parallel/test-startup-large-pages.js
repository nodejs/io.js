'use strict';

// Make sure that Node.js runs correctly with the --use-largepages option.

require('../common');
const assert = require('assert');
const { spawnSync } = require('child_process');

{
  const child = spawnSync(process.execPath,
                          [ '--use-largepages=on', '-p', '42' ],
                          { stdio: ['inherit', 'pipe', 'inherit'] });
  const stdout = child.stdout.toString().match(/\S+/g);
  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
  assert.strictEqual(stdout.length, 1);
  assert.strictEqual(stdout[0], '42');
}

{
  const child = spawnSync(process.execPath,
                          [ '--use-largepages=xyzzy', '-p', '42' ]);
  assert.strictEqual(child.status, 9);
  assert.strictEqual(child.signal, null);
  assert.match(child.stderr.toString().trim(),
               /invalid value for --use-largepages$/);
}

// TODO(gabrielschulhof): Make assertions about the stderr, which may or may not
// contain a message indicating that mapping to large pages has failed.

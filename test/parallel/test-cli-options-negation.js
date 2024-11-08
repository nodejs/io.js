'use strict';
require('../common');
const assert = require('assert');
const { spawnSync } = require('child_process');

// --warnings is on by default.
assertHasWarning(spawnWithFlags([]));

// --warnings can be passed alone.
assertHasWarning(spawnWithFlags(['--warnings']));

// --no-warnings can be passed alone.
assertHasNoWarning(spawnWithFlags(['--no-warnings']));

// Last flag takes precedence.
assertHasWarning(spawnWithFlags(['--no-warnings', '--warnings']));

// Non-boolean flags cannot be negated.
assert(spawnWithFlags(['--no-max-http-header-size']).stderr.toString().includes(
  '--no-max-http-header-size is an invalid negation because it is not ' +
  'a boolean option',
));

// Inexistent flags cannot be negated.
assert(spawnWithFlags(['--no-i-dont-exist']).stderr.toString().includes(
  'bad option: --no-i-dont-exist',
));

function spawnWithFlags(flags) {
  return spawnSync(process.execPath, [...flags, '-e', 'new Buffer(0)']);
}

function assertHasWarning(proc) {
  assert(proc.stderr.toString().includes('Buffer() is deprecated'));
}

function assertHasNoWarning(proc) {
  assert(!proc.stderr.toString().includes('Buffer() is deprecated'));
}

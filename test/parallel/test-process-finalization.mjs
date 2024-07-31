import '../common/index.mjs';
import { spawnSyncAndAssert } from '../common/child_process.js';
import fixtures from '../common/fixtures.js';

import { it } from 'node:test';

import assert from 'assert';

const files = [
  'close.mjs',
  'before-exit.mjs',
  'gc-not-close.mjs',
  'unregister.mjs',
  'different-registry-per-thread.mjs',
];

for (const file of files) {
  it(`should exit file ${file} with code=0`, () => {
    spawnSyncAndAssert(process.execPath, ['--expose-gc', `${file}`], {
      cwd: fixtures.path('process'),
    }, {
      code: 0,
    });
  });
}

it('register is different per thread', () => {
  spawnSyncAndAssert(process.execPath, ['--expose-gc', 'different-registry-per-thread.mjs'], {
    cwd: fixtures.path('process'),
  }, {
    code: 0,
    stdout: 'shutdown on worker\nshutdown on main thread\n',
  });
});

it('should throw when register undefined value', () => {
  try {
    process.finalization.register(undefined);

    assert.fail('Expected an error to be thrown for registerFreeOnExit');
  } catch (e) {
    assert.ok(e.message.includes('must be of type object'), `Expected error message to include 'Invalid' but got: ${e.message}`);
  }

  try {
    process.finalization.registerBeforeExit(undefined);

    assert.fail('Expected an error to be thrown for registerFreeOnBeforeExit');
  } catch (e) {
    assert.ok(e.message.includes('must be of type object'), `Expected error message to include 'Invalid' but got: ${e.message}`);
  }
});

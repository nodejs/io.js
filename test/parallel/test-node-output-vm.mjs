import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import * as snapshot from '../common/assertSnapshot.js';
import { describe, it } from 'node:test';

function replaceNodeVersion(str) {
  return str.replaceAll(process.version, '*');
}

describe('vm output', { concurrency: !process.env.TEST_PARALLEL }, () => {
  function normalize(str) {
    return str.replaceAll(snapshot.replaceWindowsPaths(process.cwd()), '').replaceAll('//', '*').replaceAll(/\/(\w)/g, '*$1').replaceAll('*test*', '*').replaceAll(/node:vm:\d+:\d+/g, 'node:vm:*');
  }

  const defaultTransform = snapshot
    .transform(snapshot.replaceWindowsLineEndings, snapshot.replaceWindowsPaths, normalize, replaceNodeVersion);

  const tests = [
    { name: 'vm/vm_caught_custom_runtime_error.js' },
    { name: 'vm/vm_display_runtime_error.js' },
    { name: 'vm/vm_display_syntax_error.js' },
    { name: 'vm/vm_dont_display_runtime_error.js' },
    { name: 'vm/vm_dont_display_syntax_error.js' },
  ];
  for (const { name, transform } of tests) {
    it(name, async () => {
      await snapshot.spawnAndAssert(fixtures.path(name), transform ?? defaultTransform);
    });
  }
});

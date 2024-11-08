import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import * as snapshot from '../common/assertSnapshot.js';
import { describe, it } from 'node:test';
import { basename } from 'node:path';

function replaceNodeVersion(str) {
  return str.replaceAll(process.version, '*');
}

function replaceExecName(str) {
  const baseName = basename(process.argv0 || 'node', '.exe');
  return str.replaceAll(`${baseName} --`, '* --');
}

describe('v8 output', { concurrency: !process.env.TEST_PARALLEL }, () => {
  function normalize(str) {
    return str.replaceAll(snapshot.replaceWindowsPaths(process.cwd()), '')
    .replaceAll(/:\d+/g, ':*')
    .replaceAll('/', '*')
    .replaceAll('*test*', '*')
    .replaceAll(/.*?\*fixtures\*v8\*/g, '(node:*) V8: *') // Replace entire path before fixtures/v8
    .replaceAll('*fixtures*v8*', '*');
  }
  const common = snapshot
    .transform(snapshot.replaceWindowsLineEndings, snapshot.replaceWindowsPaths, replaceNodeVersion, replaceExecName);
  const defaultTransform = snapshot.transform(common, normalize);
  const tests = [
    { name: 'v8/v8_warning.js' },
  ];
  for (const { name } of tests) {
    it(name, async () => {
      await snapshot.spawnAndAssert(fixtures.path(name), defaultTransform);
    });
  }
});

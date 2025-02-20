import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import * as snapshot from '../common/assertSnapshot.js';
import { basename } from 'node:path';
import { describe, it } from 'node:test';

describe('eval output', { concurrency: true }, () => {
  function normalize(str) {
    return str.replaceAll(snapshot.replaceWindowsPaths(process.cwd()), '')
      .replaceAll(/\d+:\d+/g, '*:*');
  }

  const defaultTransform = snapshot.transform(
    normalize,
    snapshot.replaceWindowsLineEndings,
    snapshot.replaceWindowsPaths,
    snapshot.replaceNodeVersion,
    removeStackTraces,
    filterEmptyLines,
    generalizeProcessName,
  );

  function removeStackTraces(output) {
    return output.replaceAll(/^ *at .+$/gm, '');
  }

  function filterEmptyLines(output) {
    return output.replaceAll(/^\s*$/gm, '');
  }

  function generalizeProcessName(output) {
    const baseName = basename(process.argv0 || 'node', '.exe');
    return output.replaceAll(`${baseName} --`, '* --');
  }

  const tests = [
    { name: 'eval/eval_messages.js' },
    { name: 'eval/stdin_messages.js' },
    { name: 'eval/stdin_typescript.js' },
    { name: 'eval/eval_typescript.js' },
  ];

  for (const { name } of tests) {
    it(name, async () => {
      await snapshot.spawnAndAssert(fixtures.path(name), defaultTransform);
    });
  }
});

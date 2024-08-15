import { isWindows } from '../common/index.mjs';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { opendir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { describe, it } from 'node:test';
import { sep } from 'node:path';

const python = process.env.PYTHON || (isWindows ? 'python' : 'python3');

const testRunner = fileURLToPath(
  new URL('../../tools/test.py', import.meta.url)
);

const setNames = ['async-hooks', 'parallel'];

// Get all test names for each set
const testSets = await Promise.all(setNames.map(async (name) => {
  const path = fileURLToPath(new URL(`../${name}`, import.meta.url));
  const dir = await opendir(path);

  const tests = [];
  for await (const entry of dir) {
    if (entry.name.startsWith('test-async-local-storage-')) {
      tests.push(entry.name);
    }
  }

  return {
    name,
    tests
  };
}));

// Merge test sets with set name prefix
const tests = testSets.reduce((m, v) => {
  for (const test of v.tests) {
    m.push(`${v.name}${sep}${test}`);
  }
  return m;
}, []);

describe('AsyncContextFrame', {
  concurrency: tests.length
}, () => {
  for (const test of tests) {
    it(test, async () => {
      const proc = spawn(python, [
        testRunner,
        '--node-args=--experimental-async-context-frame',
        test,
      ], {
        stdio: ['ignore', 'ignore', 'inherit'],
      });

      await once(proc, 'exit');
    });
  }
});

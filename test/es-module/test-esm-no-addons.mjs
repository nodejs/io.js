import { mustCall } from '../common/index.mjs';
import { Worker, isMainThread } from 'worker_threads';
import assert from 'assert';
import { fileURLToPath } from 'url';
import { requireFixture, importFixture } from '../fixtures/pkgexports.mjs';

if (isMainThread) {
  const tests = [[], ['--no-addons']];

  for (const execArgv of tests) {
    new Worker(fileURLToPath(import.meta.url), { execArgv });
  }
} else {
  [requireFixture, importFixture].forEach((loadFixture) => {
    loadFixture('pkgexports/no-addons').then(
      mustCall((module) => {
        const message = module.default;
        if (!process.execArgv.includes('--no-addons')) {
          assert.strictEqual(message, 'using native addons');
        } else {
          assert.strictEqual(message, 'not using native addons');
        }
      })
    );
  });
}

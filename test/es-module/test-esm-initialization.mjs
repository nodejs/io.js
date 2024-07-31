import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: ensure initialization happens only once', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it(async () => {
    const { code, stderr, stdout } = await spawnPromisified(execPath, [
      '--experimental-import-meta-resolve',
      '--loader',
      fixtures.fileURL('es-module-loaders', 'loader-resolve-passthru.mjs'),
      '--no-warnings',
      fixtures.path('es-modules', 'runmain.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    /**
     * resolveHookRunCount = 2:
     * 1. fixtures/…/runmain.mjs
     * 2. node:module (imported by fixtures/…/runmain.mjs)
     * 3. doesnt-matter.mjs (first import.meta.resolve call)
     * 4. fixtures/…/runmain.mjs (entry point)
     * 5. doesnt-matter.mjs (second import.meta.resolve call)
     */
    assert.strictEqual(stdout.match(/resolve passthru/g)?.length, 5);
    assert.strictEqual(code, 0);
  });
});
